#!/usr/bin/env python3
"""Measure the production regional tree mask on flat, fully eligible ground.

This samples the real GLSL noise/hash functions, not terrain generation timing.
Slope, coast and streaming rejection can only reduce these potential densities.
Requires EGL/OpenGL and numpy. Optional captures visualize occupied/empty groves.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"Tests/Terrain"))
from gl_headless import GL,shader_source

SOURCE=r"""#version 450 core
COMMON
layout(local_size_x=64) in;
layout(std430,binding=0) writeonly buffer Samples { vec4 samples[]; };
uniform uint uWidth,uSeed;
uniform float uSpacing,uMinimumRatio,uPeriod,uThreshold,uSoftness;
void main()
{
    uint index=gl_GlobalInvocationID.x;
    if(index>=uWidth*uWidth) return;
    ivec2 site=ivec2(index%uWidth,index/uWidth)-ivec2(int(uWidth/2u));
    uint h=objectSiteHash(site,uSeed),priority=objectHash(h^0x0fe93721u);
    vec2 jitter=objectJitter(h);
    bool allowed=true;
    for(int z=-1;z<=1;++z) for(int x=-1;x<=1;++x)
    {
        if(x==0 && z==0) continue;
        ivec2 delta=ivec2(x,z); uint other=objectSiteHash(site+delta,uSeed);
        uint rank=objectHash(other^0x0fe93721u);
        bool precedes=rank<priority || (rank==priority && (z<0 || (z==0 && x<0)));
        vec2 d=vec2(delta)+objectJitter(other)-jitter;
        if(precedes && dot(d,d)<uMinimumRatio*uMinimumRatio) allowed=false;
    }
    float mask=objectForestMask((vec2(site)+jitter)*uSpacing,uPeriod,uThreshold,uSoftness,uSeed);
    samples[index]=vec4(mask,objectRandom(h^0x315ef032u),objectRandom(h^0x35ad3191u),allowed?1.0:0.0);
}
"""


def envelope_area(catalogue,base,biome):
    """Expected projected crown bounding ellipse, including scale, not leaf area."""
    total=sum(c["weight"] for c in biome["choices"])
    if not total:return 0.0
    area=0.0
    for choice in biome["choices"]:
        group=catalogue["groups"][choice["group"]]
        for prototype in catalogue["prototypes"][group["first"]:group["first"]+group["count"]]:
            manifest=json.loads((base/prototype["asset"]/"manifest.json").read_text())
            lo,hi=manifest["boundsMin"],manifest["boundsMax"]
            a,b=prototype["minimumScale"],prototype["maximumScale"]
            ellipse=math.pi*.25*(hi[0]-lo[0])*(hi[2]-lo[2])
            area+=ellipse*(a*a+a*b+b*b)/3*choice["weight"]/total/group["count"]
    return area


def measure(gl,program,catalogue,base,seed,captures=None):
    # Fixed region spanning negative and positive world coordinates. Jittered
    # sites are uniform area probes; a 256m patch should genuinely stay empty.
    extent=16384.0
    spacing=catalogue["candidateSpacing"]
    width=int(extent/spacing)
    assert width>0 and width*spacing==extent
    buffer=gl.buffer(width*width*16,0)
    gl.uniform(program,"uWidth","1ui",width);gl.uniform(program,"uSeed","1ui",seed)
    gl.uniform(program,"uSpacing","1f",spacing)
    gl.uniform(program,"uMinimumRatio","1f",catalogue["minimumDistance"]/spacing)
    cache={};result=[]
    for biome in catalogue["biomes"]:
        if not biome["choices"] or biome["density"]==0:continue
        key=tuple(biome[k] for k in ("forestPeriod","forestThreshold","forestSoftness"))
        if key not in cache:
            for name,value in zip(("uPeriod","uThreshold","uSoftness"),key):gl.uniform(program,name,"1f",value)
            gl.bind(buffer,0);gl.dispatch(program,(width*width+63)//64)
            cache[key]=np.frombuffer(gl.read(buffer,width*width*16),dtype=np.float32).reshape(width,width,4).copy()
        data=cache[key];mask=data[:,:,0]
        assert np.isfinite(data).all() and (data>=0).all() and (data<=1).all()
        accepted=(data[:,:,1]<biome["density"]) & (data[:,:,2]<mask) & (data[:,:,3]>0)
        patch=int(256/spacing)
        empty_patches=mask.reshape(width//patch,patch,width//patch,patch).max(axis=(1,3))==0
        area=envelope_area(catalogue,base,biome)
        result.append(dict(id=biome["id"],name=biome["name"],
            sitesPerKm2=round(float(accepted.sum())/(extent/1000)**2,2),
            emptyAreaPercent=round(float((mask==0).mean())*100,2),
            empty256mPatchesPercent=round(float(empty_patches.mean())*100,2),
            fullGroveAreaPercent=round(float((mask==1).mean())*100,2),
            meanOccupancy=round(float(mask.mean()),4),
            # Sum of bounding ellipse areas in the fully wooded mask, not
            # union coverage: overlap and sparse card silhouettes reduce it.
            fullGroveEnvelopeRatio=round(area*biome["density"]*float(data[:,:,3].mean())/(spacing*spacing),3)))
        if captures and biome["id"] in (3,12,19):
            from PIL import Image
            pixels=np.zeros((width,width,3),dtype=np.uint8);pixels[:]=[31,39,38]
            pixels[mask>0]=[int(31),int(65),int(47)];pixels[accepted]=[189,201,111]
            Image.fromarray(pixels).save(captures/(biome["name"]+".png"))
    gl.check("distribution mask")
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalogue",type=Path,default=ROOT/"ENDURANCE/Ressources/Trees/object_placement.json")
    parser.add_argument("--baseline",type=Path,help="Optional old catalogue; assets resolved beside current catalogue")
    parser.add_argument("--output",type=Path)
    parser.add_argument("--captures",type=Path)
    parser.add_argument("--seed",type=int,default=1337)
    args=parser.parse_args()
    catalogue=json.loads(args.catalogue.read_text());base=args.catalogue.parent
    if args.captures:args.captures.mkdir(parents=True,exist_ok=True)
    common=shader_source(ROOT/"Asset/Shader/ProceduralTerrain/objects_common.glsl")
    with GL() as gl:
        program=gl.compile([(0x91B9,SOURCE.replace("COMMON",common))],"Tree regional distribution")
        current=measure(gl,program,catalogue,base,args.seed,args.captures)
        for biome in current:
            if biome["id"] in (3,7,8,12,13,18,19,24):
                assert 20<=biome["emptyAreaPercent"]<=80,"Forest should include substantial true clearings"
                assert biome["empty256mPatchesPercent"]>=10,"Only tiny scattered holes, no regional clearings"
                assert biome["fullGroveAreaPercent"]>=10,"Forest mask lost its dense groves"
                assert 50<=biome["sitesPerKm2"]<=700,"Default forest density drifted outside its intended budget"
        report={"seed":args.seed,"regionKm":[16.384,16.384],"renderer":gl.renderer,
            "sourceSha256":{
                "catalogue":hashlib.sha256(args.catalogue.read_bytes()).hexdigest(),
                "objects_common.glsl":hashlib.sha256(common.encode()).hexdigest(),
                "measurementScript":hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
            "current":current}
        if args.baseline:
            report["sourceSha256"]["baselineCatalogue"]=hashlib.sha256(args.baseline.read_bytes()).hexdigest()
            baseline=measure(gl,program,json.loads(args.baseline.read_text()),base,args.seed)
            report["baseline"]=baseline
            old={b["id"]:b for b in baseline}
            for b in current:b["siteRatioToBaseline"]=round(b["sitesPerKm2"]/old[b["id"]]["sitesPerKm2"],4)
    encoded=json.dumps(report,indent=2)
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(encoded+"\n")
    print(encoded)
    print("Tree distribution: regional clearings, dense groves and per-biome population budgets passed.")


if __name__=="__main__":main()
