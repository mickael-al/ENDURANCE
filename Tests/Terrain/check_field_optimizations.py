#!/usr/bin/env python3
"""Exercise compact columns, conservative skips and biome-owned controls on GL."""
from __future__ import annotations

import math
from pathlib import Path
import struct
import tempfile

from biome_mask_probe import replace_body
from biome_test_support import packed_biome_cells
from field_batch_runner import BatchFieldRunner, SHADERS
from gl_headless import GL, shader_source, copy_terrain_shaders


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def check_storage(gl):
    source="#version 450 core\n"+shader_source(SHADERS/"field_common.glsl")+"""
layout(local_size_x=64) in;
layout(std430,binding=9) readonly buffer Inputs { FieldColumn inputColumns[]; };
layout(std430,binding=10) writeonly buffer Packed { PackedFieldColumn storedColumns[]; };
layout(std430,binding=11) writeonly buffer Decoded { FieldColumn decodedColumns[]; };
uniform uint count;
void main()
{
    uint index=gl_GlobalInvocationID.x;
    if(index>=count) return;
    FieldColumn column=inputColumns[index];
    float width=column.terrain.w;
    column.terrain.w*=uDetailScale;
    PackedFieldColumn stored=fieldPackColumn(column,width);
    storedColumns[index]=stored;
    decodedColumns[index]=fieldUnpackColumn(stored);
}
"""
    program=gl.compile([(0x91B9,source)],"compact column independent storage checks")
    gl.bind(gl.buffer(packed_biome_cells()),7)
    widths=(0.,1e-8,.0001,.00019999,.0002,.00020001,.0043,.0045,1.,12.25,512.,576.)
    controls=(0.,1/65535,0.12345,.5,.99999,1.)
    records=[]
    for i,width in enumerate(widths):
        for j,control in enumerate(controls):
            offset=(-160.,-100.123,-.001,0.,80.789,160.)[j]
            ids=tuple((3*i+5*j+corner)%26 for corner in range(8))
            volume=(sum(b<<(5*c) for c,b in enumerate(ids[:6])),
                    sum(b<<(5*c) for c,b in enumerate(ids[6:])))
            values=(f32(-123.75+i),f32(.12345+j*.07),f32(.98765-i*.01),f32(width),
                    f32(.315+j*.05),f32(-65536.25+i),f32(offset),f32(control))
            records.append((values,volume,(i*7+j)%64))
    payload=b"".join(struct.pack("<8f4I",*values,cell<<20,*volume,0)
                     for values,volume,cell in records)
    gl.bind(gl.buffer(payload),9)
    sentinel=struct.pack("<I",0x7fc00001)
    stored=gl.buffer(sentinel*(len(records)*8+8),10)
    decoded=gl.buffer(sentinel*(len(records)*12+12),11)
    gl.uniform(program,"count","1ui",len(records))
    active_min=struct.unpack("<f",struct.pack("<I",struct.unpack("<I",struct.pack("<f",.0002))[0]+1))[0]
    for scale in (0.,.001,.35,1.,2.,100000.):
        gl.uniform(program,"uDetailScale","1f",scale)
        gl.dispatch(program,(len(records)+63)//64)
        raw=gl.read(stored,len(records)*32+32)
        numbers=gl.read(decoded,len(records)*48+48)
        assert raw[len(records)*32:]==sentinel*8,"Packed stride overruns its sentinel"
        assert numbers[len(records)*48:]==sentinel*12,"Decoded output overruns its sentinel"
        for index,(values,ids,cell) in enumerate(records):
            encoded=struct.unpack_from("<5f3I",raw,index*32)
            result=struct.unpack_from("<8f4I",numbers,index*48)
            assert encoded[:5]==(values[0],values[1],values[2],values[4],values[5]),"FP32 controls changed"
            assert result[:3]==values[:3] and result[4:6]==values[4:6],"FP32 decoding changed height/climate"
            assert result[9:11]==ids and ((result[8]>>20)&63)==cell,"Packing damaged biome IDs/cell"
            assert abs(result[6]-values[6])<=160/32767*.5+2e-5,"Offset exceeds declared precision"
            assert abs(result[7]-values[7])<=.5/65535+1e-7,"Extra control exceeds UNORM16 precision"
            if values[7] in (0.,1.): assert result[7]==values[7],"Control endpoint changed"
            original_width=f32(values[3]*f32(scale))
            active=original_width>f32(.0002)
            assert bool(encoded[7]&(1<<30))==active,"Packing changed the open/closed decision"
            if active:
                assert result[3]>=active_min,"Active micro-band closed during quantization"
                # The minimum nonzero code uses a conservative one-bin budget;
                # ordinary nearest rounding has half this error.
                assert abs(result[3]-original_width)<=576/65535*scale+max(2e-5,abs(original_width)*2e-7)
            else:
                assert result[3]==0.,"Closed micro-band was reopened"
    print(f"PASS compact columns: {len(records)*6} GPU round trips, 32-byte stride, exact FP32 fields/IDs, UNORM16 control and active micro-band preservation")


def edit(directory,relative,function,body):
    path=directory/relative
    path.write_text(replace_body(path.read_text(),function,body))


def check_control_and_tiles(gl):
    with tempfile.TemporaryDirectory(prefix="terrain-control-optimization-") as temporary:
        directory=Path(temporary)/"shaders"
        copy_terrain_shaders(SHADERS,directory)
        edit(directory,"biomes/PolarIce/relief.C.glsl","fieldRelief",
             "return vec4(8.0,64.0,0.0,clamp(0.5+0.25*p.x,0.0,1.0));")
        edit(directory,"biomes/PolarIce/volume.C.glsl","fieldVolume",
             "return 2.0*column.blend.w;")
        options=dict(cells=8,origin=(-4,0,-4),uHeightAmplitude=0.,uDetailScale=1.,uBiomeOverride=0)
        controlled=BatchFieldRunner(gl,directory).run(**options)
        side=controlled["side"]
        seen=set()
        for z in range(side):
            for x in range(side):
                height,control=controlled["columns"][(x+z*side)*12],controlled["columns"][(x+z*side)*12+7]
                seen.add(control)
                expected_control=min(max(.5+.25*(options["origin"][0]+x-1),0.),1.)
                assert abs(control-expected_control)<=.5/65535+1e-7
                for y in range(side):
                    expected=f32(f32(height-(y-1))+f32(2.*control))
                    assert controlled["density"][x+y*side+z*side*side]==expected,"Custom 3D control was altered externally"
        assert 0. in seen and 1. in seen and any(.49<value<.51 for value in seen)
        edit(directory,"biomes/PolarIce/volume.C.glsl","fieldVolume","return 2.0;")
        constant=BatchFieldRunner(gl,directory).run(**options)
        assert constant["columns_raw"]==controlled["columns_raw"]
        for z in range(side):
            for y in range(side):
                for x in range(side):
                    expected=f32(f32(constant["columns"][(x+z*side)*12]-(y-1))+2.)
                    assert constant["density"][x+y*side+z*side*side]==expected,"Generic layer interpreted relief.w"

        # The optional recipe helper itself must also ignore the control unless
        # the owning biome explicitly applies it to its prepared amplitudes.
        # Terrestrial recipes no longer include this optional Marine helper.
        helper_path=directory/"biomes/PolarIce/volume.C.glsl"
        helper_path.write_text(helper_path.read_text().replace(
            '#include "../../field_weights.glsl"',
            '#include "../../field_weights.glsl"\n#include "../../biome_volume_common.glsl"'))
        edit(directory,"biomes/PolarIce/volume.C.glsl","fieldVolume","""
    vec4 volume=vec4(-20,30,72,0.4);
    volume.yz*=uDetailScale;
    return fieldVolumeRecipeDelta(p,column,volume,vec4(.35,.50,.15,.10),vec4(.85,.70,.15,.30));
""")
        helper_fields=[]
        for control in (0.,1.):
            edit(directory,"biomes/PolarIce/relief.C.glsl","fieldRelief",
                 f"return vec4(8.0,64.0,0.0,{control:.1f});")
            helper_fields.append(BatchFieldRunner(gl,directory).run(**options))
        assert helper_fields[0]["density_raw"]==helper_fields[1]["density_raw"],"Default helper still interprets .w"

        # Thin active columns occupy one X tile and one Y group at this LOD.
        # The analytic scalar recipe supplies an independent complete-grid oracle.
        edit(directory,"biomes/PolarIce/relief.C.glsl","fieldRelief",
             "return vec4(0.0,p.x<256.0 ? 32.0 : 0.0,0.0,0.5);")
        edit(directory,"biomes/PolarIce/volume.C.glsl","fieldVolume","return 0.5;")
        runner=BatchFieldRunner(gl,directory)
        field=runner.run(cells=16,origin=(0,0,0),lod=6,uHeightAmplitude=0.,uDetailScale=1.,uBiomeOverride=0)
        side=field["side"]
        command=runner.last_commands[26]
        assert command[1]<(side+3)//4 and command[3]==1,"Indirect Y clipping did not reduce groups"
        assert any(tile[0]==0 for tile in runner.last_tiles[0]) and any(tile[0]!=0 for tile in runner.last_tiles[0]),"Tile fixture needs active and inactive tiles"
        for z in range(side):
            for x in range(side):
                height,band=field["columns"][(x+z*side)*12],field["columns"][(x+z*side)*12+3]
                for y in range(side):
                    world_y=(y-1)*64
                    expected=f32(height-world_y)
                    if band>0 and abs(world_y-height)<=160+.5*band: expected=f32(expected+.5)
                    assert field["density"][x+y*side+z*side*side]==expected,"Tile or Y rejection lost a contribution"
        print("PASS biome-owned .w: endpoint/midpoint custom deltas, constant independent delta, helper opt-in; thin-band tile/Y dispatch agrees with all 6,859 analytic samples")

        # Independent exhaustive launch reference: retain the same recipes,
        # packing and point-wise envelope, but remove tile/group-Y rejection and
        # dispatch the entire volume. Large +/-2^24 origins expose world-float
        # rounding at group boundaries; high LODs leave widely separated samples.
        exhaustive_directory=directory.parent/"exhaustive-shaders"
        copy_terrain_shaders(directory,exhaustive_directory)
        path=exhaustive_directory/"field_batch_prepare.C.glsl"
        source=path.read_text()
        source=source.replace("if(last>first)","if(localMasks[0].y!=0u)")
        source=source.replace("atomicMin(fieldStates[header].y,first)","atomicMin(fieldStates[header].y,0u)")
        source=source.replace("atomicMax(fieldStates[header].z,last)","atomicMax(fieldStates[header].z,groupsY)")
        path.write_text(source)
        path=exhaustive_directory/"field_volume_accumulate.glsl"
        source=path.read_text()
        first=source.index("    uint stateIndex=")
        last=source.index("    fieldSelectJob(job);",first)
        path.write_text(source[:first]+source[last:])
        exhaustive=BatchFieldRunner(gl,exhaustive_directory)
        compared=0
        clipped=False
        for sign in (-1,1):
            for lod in (0,6,12):
                origin_y=sign*(1<<24)
                height=origin_y+(-188 if lod==0 else (5 if lod==6 else 7)*(1<<lod))
                options=dict(cells=16,origin=(0,origin_y,0),lod=lod,
                             uBaseHeight=float(height),uHeightAmplitude=0.,uDetailScale=1.,uBiomeOverride=0)
                optimized=runner.run(**options)
                reference=exhaustive.run(**options)
                assert optimized["columns_raw"]==reference["columns_raw"],"Launch reference changed column data"
                assert optimized["density_raw"]==reference["density_raw"],("Large-origin tile/Y bound lost a sample",sign,lod)
                clipped|=runner.last_commands[26][1]<exhaustive.last_commands[26][1]
                affected=False
                for z in range(side):
                    for y in range(side):
                        world_y=f32(origin_y+(y-1)*(1<<lod))
                        for x in range(side):
                            base=f32(optimized["columns"][(x+z*side)*12]-world_y)
                            affected|=optimized["density"][x+y*side+z*side*side]!=base
                assert affected,("Large-origin fixture missed every contribution",sign,lod)
                compared+=side**3
        assert clipped,"Large-origin fixture never reduced a dispatch"
        print(f"PASS conservative large-origin tile/Y bounds: {compared:,} densities identical to exhaustive GPU launches at +/-2^24, LOD 0/6/12")


def check_skips_and_aliases(gl):
    production=BatchFieldRunner(gl,materialize_homogeneous=False)
    sentinel=struct.pack("<I",0x7fc00001)
    for y,sign in ((128,2),(-128,1)):
        field=production.run(cells=8,origin=(0,y,0),mode=1,uBaseHeight=0.)
        assert field["field_state"][0]==sign
        assert field["density_raw"]==sentinel*(field["side"]**3),"Homogeneous chunk unnecessarily initialized its density"
    crossing=production.run(cells=8,origin=(0,0,0),mode=1,uBaseHeight=.25)
    assert crossing["field_state"][0]==3 and all(math.isfinite(x) for x in crossing["density"])
    sphere=production.run(cells=8,origin=(0,0,0),mode=2)
    assert sphere["field_state"][0]==3 and all(math.isfinite(x) for x in sphere["density"])
    with tempfile.TemporaryDirectory(prefix="terrain-halo-culling-") as temporary:
        directory=Path(temporary)/"shaders"
        copy_terrain_shaders(SHADERS,directory)
        edit(directory,"biomes/PolarIce/relief.C.glsl","fieldRelief",
             "return vec4(p.x<0.0 ? -100.0 : 100.0,0.0,0.0,0.0);")
        # Neither height lies inside the chunk's vertical range. Their opposite
        # signs (including the single negative halo column) still require a field.
        halo=BatchFieldRunner(gl,directory,materialize_homogeneous=False).run(
            cells=8,origin=(0,0,0),uHeightAmplitude=0.,uDetailScale=1.,uBiomeOverride=0)
        assert halo["field_state"][0]==3 and all(math.isfinite(x) for x in halo["density"]),"Surface/halo sign union incorrectly culled a chunk"

    runner=BatchFieldRunner(gl)
    requests=[dict(origin=(-8,-8,-8),seed=1337),
              dict(origin=(-8,0,-8),seed=1337,column_slot=0,flags=5),
              dict(origin=(-8,-8,-8),seed=1337,column_slot=0,density_slot=0,flags=1)]
    fields=runner.run_batch(requests,cells=8,padding=True)
    assert fields[0]["columns_raw"]==fields[1]["columns_raw"]==fields[2]["columns_raw"]
    assert fields[0]["density_raw"]==fields[2]["density_raw"]
    assert all(command[3]<=1 for command in runner.last_commands[:26]),"Shared columns were rebuilt more than once"
    for request,field in zip(requests,fields):
        separate=runner.run(cells=8,origin=request["origin"],seed=request["seed"])
        assert separate["columns_raw"]==field["columns_raw"] and separate["density_raw"]==field["density_raw"],"Cached alias differs from independent generation"
    print("PASS homogeneous production skips, sphere/halo conservatism, vertical column aliases and density aliases with sentinel-protected strides")


def main():
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_storage(gl)
        check_control_and_tiles(gl)
        check_skips_and_aliases(gl)
        gl.check("optimized field contracts")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
