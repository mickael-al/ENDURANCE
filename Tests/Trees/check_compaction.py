#!/usr/bin/env python3
"""Compare packed object compaction against the former per-type slab shader.

Both programs use the same production hashing/quantization functions. This
isolates the layout/atomics change from terrain sampling and checks all type
counts, site multisets, zero overwrites, input integrity and output bounds.
"""
import argparse
from array import array
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"Tests/Terrain"))
from gl_headless import GL, shader_source

REFERENCE=r"""
layout(local_size_x=64) in;
layout(std430,binding=3) buffer Counts { uint objectCounts[]; };
uniform uint uObjectCountBase;
void main() {
    uint site=gl_GlobalInvocationID.x,count=objectWords[7];
    if(site>=count) return;
    uint hit=objectWords[uObjectJobBase+8u+site];
    if(hit==0u) return;
    uint type=hit&255u;
    if(type>=objectWords[0]) return;
    uint index=atomicAdd(objectCounts[uObjectCountBase+type],1u);
    if(index>=count) return;
    uint side=objectWords[3],step=objectWords[2];
    uvec2 local=uvec2(site%side,(site/side)%side);
    ivec2 origin=ivec2(objectWords[uObjectJobBase],objectWords[uObjectJobBase+2u]);
    ivec2 worldSite=origin/int(step)+ivec2(local);
    uint h=objectSiteHash(worldSite,objectWords[uObjectJobBase+3u]);
    vec2 point=(vec2(local)+objectJitter(h))/float(side);
    uvec2 xz=uvec2(floor(point*65535.0+0.5));
    uint y=(hit>>8u)-1u;
    uint outputBase=uObjectJobBase+8u+count+2u*(type*count+index);
    objectWords[outputBase]=xz.x|(y<<16u);
    objectWords[outputBase+1u]=xz.y|(site<<16u);
}
"""


def words(data):
    result=array("I"); result.frombytes(data); return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    directory=ROOT/"Asset/Shader/ProceduralTerrain"
    common=shader_source(directory/"objects_common.glsl")
    report={"scope":"GPU exact compaction equivalence, not target-hardware timing",
            "source_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
            (directory/"objects_common.glsl",directory/"objects_compact.C.glsl")},"cases":[]}
    sentinel=0xdeadbeef; base=32; count_base=4
    with GL() as gl:
        report["renderer"]=gl.renderer
        reference=gl.compile([(0x91B9,"#version 450 core\n"+common+REFERENCE)],"old per-type slab oracle")
        for sites,types in ((1,1),(64,3),(256,102),(1024,255),(65536,3)):
            defines=f"#define TERRAIN_OBJECT_SITE_COUNT {sites}\n#define TERRAIN_OBJECT_TYPE_COUNT {types}\n"
            current=gl.compute(directory/"objects_compact.C.glsl",defines)
            # Reuse allocations between cases to expose stale type counters.
            outputs=2*sites; old_outputs=outputs*types
            buffers=[gl.buffer((base+8+sites+n+16)*4) for n in (old_outputs,outputs)]
            count_buffers=[gl.buffer((count_base+types+4)*4) for _ in range(2)]
            side=128 if sites>=16384 else 16 if sites>=256 else 8 if sites>=64 else 1
            for pattern in ("full","sparse","one-type","invalid","empty","full-again"):
                hits=[]
                for site in range(sites):
                    type_id=(site*97+site//7)%types
                    if pattern=="one-type": type_id=types-1
                    if pattern=="invalid" and site%3==0: type_id=255
                    height=(site*881+31)%65536
                    hit=((height+1)<<8)|type_id
                    if pattern=="empty" or (pattern=="sparse" and site%7): hit=0
                    hits.append(hit)
                received=[]; counts_received=[]
                for index,(program,n) in enumerate(((reference,old_outputs),(current,outputs))):
                    payload=array("I",[sentinel]*(base+8+sites+n+16))
                    payload[0]=types;payload[2]=8;payload[3]=side;payload[7]=sites;payload[15]=8
                    payload[base:base+8]=array("I",[(-128)&0xffffffff,0,(-256)&0xffffffff,1337,0,0,side,sites])
                    payload[base+8:base+8+sites]=array("I",hits)
                    counters=array("I",[sentinel]*(count_base+types+4))
                    if index==0: counters[count_base:count_base+types]=array("I",[0]*types)
                    gl.write(buffers[index],payload.tobytes());gl.write(count_buffers[index],counters.tobytes())
                    gl.bind(buffers[index],7);gl.bind(count_buffers[index],3)
                    gl.uniform(program,"uObjectJobBase","1ui",base)
                    gl.uniform(program,"uObjectCountBase","1ui",count_base)
                    gl.dispatch(program,(sites+63)//64 if index==0 else 1)
                    actual=words(gl.read(buffers[index],len(payload)*4))
                    actual_counts=words(gl.read(count_buffers[index],len(counters)*4))
                    assert actual[:base+8+sites]==payload[:base+8+sites],"Compaction modified source records"
                    assert actual[-16:]==payload[-16:],"Compaction wrote beyond its bounded output"
                    assert actual_counts[:count_base]==counters[:count_base] and actual_counts[-4:]==counters[-4:],"Count range escaped"
                    counts=list(actual_counts[count_base:count_base+types]);counts_received.append(counts)
                    assert sum(counts)<=sites
                    first=base+8+sites;points=[]
                    for type_id,count in enumerate(counts):
                        start=first+2*type_id*sites if index==0 else first
                        for i in range(count): points.append((type_id,actual[start+2*i],actual[start+2*i+1]))
                        if index==1: first+=2*count
                    received.append(sorted(points))
                assert counts_received[0]==counts_received[1],"Prototype histogram changed"
                assert received[0]==received[1],"Prototype/site packed position multiset changed"
                gl.check("Object compaction oracle")
                report["cases"].append({"sites":sites,"types":types,"pattern":pattern,"positions":len(received[1]),
                    "old_output_bytes":old_outputs*4,"new_output_bytes":outputs*4})
    report["identical_positions"]=sum(c["positions"] for c in report["cases"])
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS {len(report['cases'])} compaction cases, {report['identical_positions']} identical packed positions; zero/stale/invalid-type/bounds checks")

if __name__=="__main__": main()
