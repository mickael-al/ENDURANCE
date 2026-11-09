#!/usr/bin/env python3
"""Validate fixed per-biome accumulation, GPU work lists and independent recipes."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import re
import struct
import tempfile

from biome_mask_probe import replace_body
from check_gpu import FieldRunner, LegacyFieldRunner, MeshRunner, SHADERS, lattice_values, finite_columns
from field_batch_runner import BIOMES, BatchFieldRunner, MAX_JOBS
from gl_headless import GL, shader_source, copy_terrain_shaders


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def compare(a, b, label):
    assert a["columns_raw"] == b["columns_raw"], label+": columns differ"
    assert a["density_raw"] == b["density_raw"], label+": density differs"


def check_pipeline(gl):
    runner=BatchFieldRunner(gl)
    cases=(
        dict(cells=8,origin=(-8,-8,-8),seed=1337),
        dict(cells=8,origin=(0,-8,-8),seed=1337),
        dict(cells=8,origin=(-16,-16,-16),seed=1337,lod=1),
        dict(cells=16,origin=(-48,-64,16),seed=17,uClimateFrequency=.04,
             uHeightAmplitude=240.,uHeightFrequency=.03),
        dict(cells=8,origin=(-8,-8,-8),seed=19,uSeaLevel=500.),
    )
    outputs=[]
    for index,options in enumerate(cases):
        field=runner.run(**options)
        compare(field,runner.run(**options),f"Repeated automatic field {index}")
        assert finite_columns(field["columns"]) and all(math.isfinite(x) for x in field["density"])
        outputs.append(field)
    fine,neighbor,coarse=map(lattice_values,outputs[:3])
    for other in (neighbor,coarse):
        shared=fine.keys() & other.keys()
        assert shared and all(fine[p]==other[p] for p in shared), "Adjacent chunks or LOD halos disagree"
    for biome in range(26):
        options=dict(cells=8,origin=(-8,-8,-8),uBiomeOverride=biome,
                     uHeightAmplitude=0.,seed=31)
        field=runner.run(**options)
        assert finite_columns(field["columns"]) and all(math.isfinite(x) for x in field["density"])
        compare(field,runner.run(**options),f"Repeated biome {biome}")
        # A forced biome can only select its own specialization. No stale queue
        # from the previous forced ID may run a different recipe.
        for dimension in (0,1):
            for other,command in enumerate(runner.last_commands[dimension*26:(dimension+1)*26]):
                if other!=biome:
                    assert command[2:]==(0,0),("Unexpected forced-biome work",biome,dimension,other,command)
    options=dict(cells=8,origin=(-8,0,-8),uBiomeOverride=11,
                 uHeightAmplitude=0.,seed=31)
    surface=runner.run(**options)
    options["origin"]=(-8,math.floor(surface["columns"][0]/8)*8,-8)
    mesh=MeshRunner(gl)
    first=mesh.run(runner.run(**options));second=mesh.run(runner.run(**options))
    assert first["count"]==second["count"] and first["count"]>0, "Mesh fixture missed the surface"
    assert sorted(zip(first["vertices"],first["normals"]))==sorted(zip(second["vertices"],second["normals"])), "Repeated mesh differs"
    print("PASS fixed biome pipeline: all 52 variants, 26 forced recipes, deterministic fields/meshes, zero absent-biome dispatches and shared LOD/halo samples")
    return runner


def verify_queues(runner, requests, side):
    for queue,command in enumerate(runner.last_commands):
        dimension,biome=divmod(queue,26)
        expected=[slot for slot,request in enumerate(requests)
                  if request is not None and (runner.last_jobs[slot][5] & (4 if dimension else 2))
                  and (runner.last_jobs[slot][6+dimension] & (1<<biome))]
        assert command[3]==len(expected),("Wrong compact queue count",queue,command,expected)
        actual=runner.last_work_items[queue*MAX_JOBS:queue*MAX_JOBS+len(expected)]
        assert actual==tuple(expected),("Work list lost/reordered/duplicated a slot",queue,actual,expected)
        y_groups=max((runner.last_states[runner.last_jobs[slot][10]][2]-runner.last_states[runner.last_jobs[slot][10]][1]
                      for slot in expected),default=0) if dimension else (side+7)//8
        target=((side+7)//8,y_groups,
                len(expected)*((side+3)//4 if dimension else 1))
        assert command[:3]==target,("Unsafe indirect dimensions",queue,command,target)


def check_batches(gl,runner):
    requests=[None if i in (2,5,11) else dict(origin=(i*16-64,-16,(i%4)*16-32),
              lod=i%2,seed=1337+17*i) for i in range(MAX_JOBS)]
    options=dict(cells=8,uClimateFrequency=.04,uHeightAmplitude=32.,uHeightFrequency=.02)
    batch=runner.run_batch(requests,padding=True,**options)
    verify_queues(runner,requests,11)
    for slot,request in enumerate(requests):
        if request is not None:
            compare(batch[slot],runner.run(**request,**options),f"Batched slot {slot}")
    # The same queue storage is reused with fewer active slots and no biome
    # work. Every absent command must become zero even if it was active earlier.
    empty=[None,dict(origin=(0,0,0),seed=19),None,None]
    runner.run_batch(empty,cells=9,mode=1,padding=True)
    verify_queues(runner,empty,12)
    assert all(command[2:]==(0,0) for command in runner.last_commands)
    print("PASS GPU work lists: 16 slots, distinct seeds/LODs, inactive holes, sentinel-protected strides, compact lists and zeroed commands after reuse")


def check_envelope_rounding(gl):
    # Reassociating (height + globalOffset) + recipeOffset loses a different
    # low bit than the actual helper's height + (globalOffset + recipeOffset).
    # This point is inside the true band despite exceeding the old upper bound.
    source="#version 450 core\n"+shader_source(SHADERS/"field_weights.glsl")
    source+=shader_source(SHADERS/"biome_volume_common.glsl")+"""
layout(local_size_x=1) in;
layout(std430,binding=9) writeonly buffer ProbeResult { vec4 result; };
void main()
{
    FieldColumn column;
    column.terrain=vec4(-806157.9375,0.5,0.5,402.4158326);
    column.blend=vec4(0.0,-617988.75,0.0,1.0);
    column.detail=uvec4(0u);
    vec2 band=fieldVolumeBand(column,160.0);
    vec2 envelope=fieldVolumeEnvelope(column);
    float edgeDistance=band.y-abs(-1423813.125-band.x);
    result=vec4(envelope,band.x,edgeDistance);
}
"""
    program=gl.compile([(0x91B9,source)],"conservative band rounding regression")
    output=gl.buffer(16,9)
    gl.uniform(program,"uDetailScale","1f",.8271826506)
    gl.dispatch(program,1)
    lower,upper,center,edge=struct.unpack("<4f",gl.read(output,16))
    assert center==-1424014.25 and edge>0,("Regression fixture no longer samples its true band interior",center,edge)
    assert lower<=-1423813.125<=upper,("Rounded envelope dropped an interior band point",lower,upper,center,edge)
    print("PASS conservative envelope: large negative height/offset rounding retains the known interior band point")


def packed_cell(surface,volume):
    mask=sum(1<<biome for biome in set(volume))
    return (sum(b<<(5*i) for i,b in enumerate(surface)),
            sum(b<<(5*i) for i,b in enumerate(volume[:6])),
            sum(b<<(5*i) for i,b in enumerate(volume[6:])),mask)


def corner_weights(fx,fy,fz,ids):
    result=[0.]*26
    for corner,biome in enumerate(ids):
        result[biome]+=(fx if corner&1 else 1-fx)*(fy if corner&2 else 1-fy)*(fz if corner&4 else 1-fz)
    return result


def local_weights(column,words,override=-1):
    if override>=0:
        result=[float(b==override) for b in range(26)]
        return result,result
    def fade(value):
        return value**3*(value*(value*6-15)+10)
    ratios=[]
    for value in column[1:3]:
        grid=min(max(value,0.),1.)*4
        ratios.append(fade(grid-min(math.floor(grid),3)))
    fx,fy=ratios
    altitude=column[4]
    cell=(words[8]>>20)&63
    surface_ids=[(words[8]>>(5*i))&31 for i in range(4)]
    volume_ids=[(words[9]>>(5*i))&31 for i in range(6)]+[(words[10]>>(5*i))&31 for i in range(2)]
    marine=1-altitude if cell<16 else 0.
    surface=corner_weights(fx,fy,0.,surface_ids+[0]*4)
    surface=[weight*(1-marine) for weight in surface]
    surface[25]+=marine
    return surface,corner_weights(fx,fy,altitude,volume_ids)


def check_weighted_oracle(gl):
    """Use independent CPU corner sums and visibly different scalar functions.

    Temporary recipes have analytic outputs, so the expected final density does
    not call the production accumulation or interpolation helpers at all.
    """
    with tempfile.TemporaryDirectory(prefix="terrain-accumulation-oracle-") as temporary:
        directory=Path(temporary)/"shaders"
        copy_terrain_shaders(SHADERS,directory)
        for biome,name in enumerate(BIOMES):
            path=directory/f"biomes/{name}/relief.C.glsl"
            path.write_text(replace_body(path.read_text(),"fieldRelief",
                f"return vec4({4*(biome-13)}.0+0.125*p.x+0.0625*p.y,"
                f"{192+biome}.0,{.5*(biome-13):.1f},{.25+.5*(biome%2):.2f});"))
            path=directory/f"biomes/{name}/volume.C.glsl"
            path.write_text(replace_body(path.read_text(),"fieldVolume",
                f"return {1+biome*biome}.0+0.03125*p.x-0.0625*p.y+0.015625*p.z;"))
        runner=BatchFieldRunner(gl,directory)
        cases=[
            (dict(cells=9,origin=(-16,-16,-16),seed=1337,uClimateFrequency=.04,
                  uHeightAmplitude=0.,uBaseHeight=0.,uDetailScale=.5),None),
            (dict(cells=8,origin=(-32,-16,16),seed=19,lod=1,uClimateFrequency=.03,
                  uHeightAmplitude=0.,uBaseHeight=16.,uDetailScale=.5,uSeaLevel=-1000.),None),
            (dict(cells=8,origin=(-16,-16,-16),seed=17,uClimateFrequency=.04,
                  uHeightAmplitude=0.,uBaseHeight=0.,uDetailScale=.5),
             packed_cell((0,0,25,25),(0,1,2,3,20,21,24,25))),
        ]
        # Force all IDs individually too, including the marine override whose
        # display climate is intentionally distinct from the land coordinates.
        cases += [(dict(cells=4,origin=(-4,-4,-4),seed=31,uBiomeOverride=biome,
                        uHeightAmplitude=0.,uBaseHeight=0.,uDetailScale=.5),None)
                  for biome in range(26)]
        total=0
        largest=0.
        for options,mapping in cases:
            if mapping is not None:
                gl.write(runner.mapping,struct.pack("<4I",*mapping)*64)
            else:
                from biome_test_support import packed_biome_cells
                gl.write(runner.mapping,packed_biome_cells())
            field=runner.run(**options)
            side=field["side"]
            scale=options["uDetailScale"]
            spacing=options.get("uBaseSpacing",1.)
            stride=1<<options.get("lod",0)
            origin=options["origin"]
            for z in range(side):
                for x in range(side):
                    address=(x+z*side)*48
                    column=struct.unpack_from("<12f",field["columns_raw"],address)
                    words=struct.unpack_from("<12I",field["columns_raw"],address)
                    surface,volume=local_weights(column,words,options.get("uBiomeOverride",-1))
                    world_x=f32((origin[0]+(x-1)*stride)*spacing)
                    world_z=f32((origin[2]+(z-1)*stride)*spacing)
                    relief=[sum(w*(4*(b-13)+.125*world_x+.0625*world_z,192+b,.5*(b-13),.25+.5*(b%2))[j]
                                for b,w in enumerate(surface)) for j in range(4)]
                    expected=(options["uBaseHeight"]+relief[0]*scale,relief[1]*scale,relief[2],relief[3])
                    actual=(column[0],column[3],column[6],column[7])
                    tolerance=(1e-4,576/65535*scale*.5+1e-4,160/32767*.5+1e-4,1/65535*.5+1e-5)
                    assert all(math.isclose(a,b,rel_tol=2e-5,abs_tol=eps) for a,b,eps in zip(actual,expected,tolerance)),("2D accumulation/quantization",options,x,z,actual,expected)
                    for y in range(side):
                        world_y=f32((origin[1]+(y-1)*stride)*spacing)
                        base=f32(column[0]-world_y)
                        radius=160*scale+.5*column[3]
                        inside=abs(world_y-(column[0]+column[5]))<=radius
                        expected_density=base
                        if inside and column[3]>.0002:
                            expected_density+=sum(w*(1+b*b+.03125*world_x-.0625*world_y+.015625*world_z)
                                                  for b,w in enumerate(volume))
                        actual=field["density"][x+y*side+z*side*side]
                        largest=max(largest,abs(actual-expected_density))
                        assert math.isclose(actual,expected_density,rel_tol=2e-5,abs_tol=1e-4),("3D weighted delta",options,x,y,z,actual,expected_density)
                        total+=1
        # Fractional positive weights well below 0.1% must survive. A chunk-wide
        # mean/max threshold would suppress the analytic contribution here.
        classify=directory/"field_batch_classify.C.glsl"
        source=classify.read_text()
        source=re.sub(r"vec2 climate=smoothstep[\s\S]*?;", "vec2 climate=vec2(0.25625);",source,count=1)
        classify.write_text(source)
        tiny=BatchFieldRunner(gl,directory)
        field=tiny.run(cells=4,origin=(0,0,0),uHeightAmplitude=0.,uSeaLevel=-1000.,uDetailScale=.5)
        column=struct.unpack_from("<12f",field["columns_raw"],0)
        words=struct.unpack_from("<12I",field["columns_raw"],0)
        _,weights=local_weights(column,words)
        assert any(0<w<.001 for w in weights)
        expected=column[0]+1+sum(w*(1+b*b-.03125+.0625-.015625) for b,w in enumerate(weights))
        dropped=column[0]+1+sum(w*(1+b*b-.03125+.0625-.015625) for b,w in enumerate(weights) if w>=.001)
        assert abs(field["density"][0]-expected)<.0005 and abs(field["density"][0]-dropped)>.005,("Tiny positive weights were removed",field["density"][0],expected,dropped)
        print(f"PASS independent weighted oracle: {total} densities, all 26 IDs, duplicate IDs, eight volume contributors, sea and sub-0.1% local weights (max error {largest:.6g})")


def check_free_recipes(gl,runner):
    options=dict(cells=8,origin=(-8,-8,-8),seed=1337,uBiomeOverride=0,uHeightAmplitude=0.)
    before=runner.run(**options)
    other=runner.run(**dict(options,uBiomeOverride=11))
    with tempfile.TemporaryDirectory(prefix="terrain-free-recipes-") as temporary:
        directory=Path(temporary)/"shaders"
        copy_terrain_shaders(SHADERS,directory)
        path=directory/"biomes/PolarIce/volume.C.glsl"
        path.write_text(replace_body(path.read_text(),"fieldVolume", """
    vec2 band=fieldVolumeBand(column,0.0);
    float radial=length(vec3(p.x,0.0,p.z)-vec3(0.0));
    float rock=max(0.0,4.0-radial);
    return rock*smoothstep(0.0,1.0,band.y-abs(p.y-band.x));
"""))
        changed=BatchFieldRunner(gl,directory)
        after=changed.run(**options)
        assert before["columns_raw"]==after["columns_raw"],"3D edit changed 2D data"
        assert before["density_raw"]!=after["density_raw"],"Independent custom 3D function had no effect"
        compare(other,changed.run(**dict(options,uBiomeOverride=11)),"Other biome after polar 3D edit")
        disabled=dict(options,uDetailScale=0.)
        compare(runner.run(**disabled),changed.run(**disabled),"Disabled 3D detail")
        path=directory/"biomes/PolarIce/relief.C.glsl"
        path.write_text(replace_body(path.read_text(),"fieldRelief", """
    float height=24.0+noiseCellularDistance2D(p*0.03125, uSeed ^ (714u), NOISE_MANHATTAN)*16.0;
    return vec4(height,0.0,0.0,1.0);
"""))
        changed=BatchFieldRunner(gl,directory)
        after=changed.run(**options)
        assert before["columns_raw"]!=after["columns_raw"] and before["density_raw"]!=after["density_raw"]
        compare(other,changed.run(**dict(options,uBiomeOverride=11)),"Other biome after polar 2D edit")
        compare(runner.run(**disabled),changed.run(**disabled),"Disabled 2D detail")
    print("PASS independent recipe edits: custom polar 3D geometry and 2D Voronoi change only their biome; zero detail disables both")


def check_reference(gl,runner,directory):
    old=LegacyFieldRunner(gl,directory=directory)
    largest=0.
    samples=0
    for biome in range(26):
        for seed in (1337,98765):
            options=dict(cells=8,origin=(-40,-12,24),lod=3,seed=seed,
                         uBaseSpacing=4.,uBiomeOverride=biome,uHeightAmplitude=0.,uDetailScale=.35)
            before,after=old.run(**options),runner.run(**options)
            # The new packed surface IDs intentionally occupy formerly unused
            # column metadata; numeric channels retain the single-biome profile.
            assert all(math.isclose(a,b,rel_tol=2e-5,abs_tol=4e-5)
                       for i,(a,b) in enumerate(zip(before["columns"],after["columns"])) if i%12<8)
            for a,b in zip(before["density"],after["density"]):
                largest=max(largest,abs(a-b))
                assert math.isclose(a,b,rel_tol=2e-5,abs_tol=4e-5),("Forced biome changed",biome,seed,a,b)
                samples+=1
    print(f"PASS prior single-biome appearance: {samples} densities / 26 biomes / two seeds (max error {largest:.6g})")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-shaders",type=Path,help="Saved pre-accumulation shader directory")
    args=parser.parse_args()
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        runner=check_pipeline(gl)
        check_batches(gl,runner)
        check_envelope_rounding(gl)
        check_weighted_oracle(gl)
        check_free_recipes(gl,runner)
        if args.reference_shaders:
            check_reference(gl,runner,args.reference_shaders)
        gl.check("biome accumulation pipeline")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
