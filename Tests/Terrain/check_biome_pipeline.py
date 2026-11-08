#!/usr/bin/env python3
"""Verify the generic biome pipeline, packed CPU matrices, editable reliefs and 3D parameters."""
import argparse
import math
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

from biome_test_support import biome_exporter
from biome_mask_probe import check_biome_masks, check_volume_noise_budget
from check_gpu import FieldRunner, MeshRunner, SHADERS, lattice_values, finite_columns
from gl_headless import GL


def compare(a,b,label):
    assert a["columns_raw"] == b["columns_raw"], label+": columns differ"
    assert a["density_raw"] == b["density_raw"], label+": density differs"


def check_pipeline(gl):
    generic=FieldRunner(gl)
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
        field=generic.run(**options)
        compare(field,generic.run(**options),f"Repeated automatic field {index}")
        assert finite_columns(field["columns"]) and all(math.isfinite(x) for x in field["density"])
        outputs.append(field)
    fine,neighbor,coarse=map(lattice_values,outputs[:3])
    for other in (neighbor,coarse):
        shared=fine.keys() & other.keys()
        assert shared and all(fine[p]==other[p] for p in shared), "Adjacent chunks or LOD halos disagree"
    # One linked pair must support every biome without recompiling programs.
    for biome in range(26):
        options=dict(cells=8,origin=(-8,-8,-8),uBiomeOverride=biome,
                     uHeightAmplitude=0.,seed=31)
        field=generic.run(**options)
        assert finite_columns(field["columns"]) and all(math.isfinite(x) for x in field["density"])
        compare(field,generic.run(**options),f"Repeated biome {biome}")
    options=dict(cells=8,origin=(-8,0,-8),uBiomeOverride=11,
                 uHeightAmplitude=0.,seed=31)
    surface=generic.run(**options)
    options["origin"]=(-8,math.floor(surface["columns"][0]/8)*8,-8)
    mesh=MeshRunner(gl)
    first=mesh.run(generic.run(**options));second=mesh.run(generic.run(**options))
    assert first["count"]==second["count"] and first["count"]>0, "Mesh fixture missed the surface"
    assert sorted(zip(first["vertices"],first["normals"]))==sorted(zip(second["vertices"],second["normals"])), "Repeated mesh differs"
    print("PASS generic biome pipeline: 26 recipes, deterministic fields/meshes, local ratios, negative coordinates and shared LOD/halo samples")
    return generic


def check_shared_volume(gl,generic):
    """Verify shared preparation and whole-group rejection against scalar GLSL."""
    with tempfile.TemporaryDirectory(prefix="terrain-direct-volume-") as temporary:
        directory=Path(temporary)/"shaders"
        shutil.copytree(SHADERS,directory)
        (directory/"field_density.C.glsl").write_text("""#version 450 core
#include "field_volume.glsl"
layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) writeonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { FieldColumn columns[]; };
void main()
{
    uvec3 p=gl_GlobalInvocationID;
    uint side=uCells+3u;
    if(any(greaterThanEqual(p,uvec3(side)))) return;
    density[p.x+p.y*side+p.z*side*side]=fieldDensityAt(
        fieldWorldPosition(ivec3(p)),columns[p.x+p.z*side]);
}
""")
        direct=FieldRunner(gl,directory=directory)
        cases=[]
        for biome in (-1,0,19,25):
            for lod in (0,2):
                for y in (-256,-96,-48,-16,0,32,128):
                    cases.append(dict(cells=8,origin=(-24,y,-16),lod=lod,seed=31,
                        uBiomeOverride=biome,uHeightAmplitude=0.,uBaseHeight=0.,
                        uBandOffset=-7.5,
                        uBandOffsetVariation=16.,uDetailScale=.35,uClimateFrequency=.04))
        # Non-power-of-two spacing, empty volumes and the last partial Y group.
        for scale,spacing in ((0.,1.),(.35,.3),(.35,3.7)):
            cases.append(dict(cells=9,origin=(-32,-24,-16),seed=19,
                uBaseSpacing=spacing,uDetailScale=scale,uBiomeOverride=0,
                uHeightAmplitude=0.))
        for height in (-1048576.,1048576.):
            for offset in (-80,0,80):
                cases.append(dict(cells=9,origin=(-24,int(height)+offset,-16),lod=1,seed=31,
                    uBiomeOverride=19,uBaseHeight=height,uHeightAmplitude=0.,
                    uDetailScale=.35))
        samples=0
        for index,options in enumerate(cases):
            actual=generic.run(**options)
            expected=direct.run(**options)
            compare(actual,expected,f"Shared versus scalar volume {index}")
            samples+=len(actual["density"])
    print(f"PASS shared volume preparation/rejection: {len(cases)} cases / {samples} densities bitwise equal to per-voxel evaluation; band edges, distant negative/positive origins, partial groups, LODs and nonbinary spacing")


def check_volume_table_edit(gl, generic):
    options = dict(cells=8, origin=(-8,-8,-8), seed=1337, uBiomeOverride=0,
                   uHeightAmplitude=0.)
    before = generic.run(**options)
    other_before = generic.run(**dict(options,uBiomeOverride=11))
    with tempfile.TemporaryDirectory(prefix="terrain-free-volume-") as temporary:
        directory = Path(temporary)/"shaders"
        shutil.copytree(SHADERS, directory)
        path = directory/"biome_volume.glsl"
        source = path.read_text()
        start=source.index("FIELD_BIOME_VOLUME")
        row=re.search(r"vec4\s*\([^()]*\)",source[start:])
        assert row, "Missing polar volume row"
        first,last=start+row.start(),start+row.end()
        # Change only polar offset/displacement/carving. Other table rows and
        # the common evaluator stay identical, as during ordinary shader tuning.
        path.write_text(source[:first]+"vec4(0.0,32.0,0.0,0.40)"+source[last:])
        edited = FieldRunner(gl,directory=directory)
        after = edited.run(**options)
        assert before["columns_raw"] == after["columns_raw"], "Editing the volume table changed the 2D preparation"
        assert before["density_raw"] != after["density_raw"], "Editing polar volume parameters did not change density"
        assert all(math.isfinite(x) for x in after["density"])
        other_after = edited.run(**dict(options,uBiomeOverride=11))
        compare(other_before,other_after,"Unedited biome after polar 3D edit")
        disabled=dict(options,uDetailScale=0.)
        compare(generic.run(**disabled),edited.run(**disabled),"Disabled 3D details after recipe edit")
    print("PASS editable 3D table: polar parameter edit changes density, preserves 2D columns and leaves another biome unchanged")


def check_free_surface(gl, generic):
    options=dict(cells=8,origin=(-8,-8,-8),seed=1337,uBiomeOverride=0,
                 uHeightAmplitude=0.)
    before=generic.run(**options)
    other_before=generic.run(**dict(options,uBiomeOverride=11))
    with tempfile.TemporaryDirectory(prefix="terrain-free-surface-") as temporary:
        directory=Path(temporary)/"shaders"
        shutil.copytree(SHADERS,directory)
        path=directory/"biome_relief.glsl"
        source=path.read_text()
        body=source.index("{",source.index("vec4 fieldReliefPolarIce("))
        injected="""
    float customHeight=24.0+fieldCellular2(p*0.03125,714u,1.0)*16.0;
    return vec4(customHeight,0.0,0.0,1.0);
"""
        path.write_text(source[:body+1]+injected+source[body+1:])
        edited=FieldRunner(gl,directory=directory)
        after=edited.run(**options)
        assert before["columns_raw"]!=after["columns_raw"], "Custom 2D recipe did not change columns"
        assert before["density_raw"]!=after["density_raw"], "Custom 2D recipe did not change actual density"
        assert finite_columns(after["columns"]) and all(math.isfinite(x) for x in after["density"])
        compare(other_before,edited.run(**dict(options,uBiomeOverride=11)),"Unedited biome after polar 2D edit")
        disabled=dict(options,uDetailScale=0.)
        compare(generic.run(**disabled),edited.run(**disabled),"Disabled 2D details after recipe edit")
    print("PASS free 2D recipe: temporary Manhattan Voronoi changes columns/density, preserves other biomes and detailScale=0")


def check_reference(gl, generic, directory):
    """Compare a prior evaluator, allowing the explicit global-band migration."""
    old=FieldRunner(gl,directory=directory)
    parameter_reference="FIELD_BIOME_VOLUME" in (directory/"biome_volume.glsl").read_text()
    legacy_band="uBandThickness" in (directory/"field_common.glsl").read_text()
    if legacy_band:
        gl.uniform(old.columns_program,"uBandThickness","1f",64.)
        gl.uniform(old.columns_program,"uBandVariation","1f",16.)
    cases=[]
    for biome in range(26):
        for seed in (1337,98765):
            cases.append(dict(cells=8,origin=(-40,-12,24),lod=3,seed=seed,
                uBaseSpacing=4.,uBiomeOverride=biome,uHeightAmplitude=0.,
                uDetailScale=.35))
    for lod in (0,1,3):
        for seed in (17,1337):
            cases.append(dict(cells=8,origin=(-48,-64,16),lod=lod,seed=seed,
                uClimateFrequency=.04,uHeightAmplitude=240.,uHeightFrequency=.03,
                uBandOffset=-32.,uSeaLevel=16.))
    cases += [dict(cells=8,origin=(-8,-4,-12),mode=mode,seed=31)
              for mode in (1,2)]
    samples=0
    largest_error=0.
    changed_mixed=0
    def live_columns(raw):
        return b"".join(raw[i:i+12]+(b"" if legacy_band else raw[i+12:i+16])+
                        raw[i+16:i+32]+raw[i+36:i+48] for i in range(0,len(raw),48))
    for index,options in enumerate(cases):
        before,after=old.run(**options),generic.run(**options)
        # The unused polar-activity word is reserved/zero. Height, climate,
        # offset and biome IDs stay exact; migrated band widths are checked below.
        assert live_columns(before["columns_raw"])==live_columns(after["columns_raw"]), ("Reference column changed",index)
        if legacy_band:
            for a,b in zip(before["columns"][3::12],after["columns"][3::12]):
                assert abs(a-b)<8.101,("Retuned biome band changed beyond removed global variation",index,a,b)
            assert all(math.isfinite(v) for v in after["density"])
            samples+=len(after["density"])
        elif parameter_reference or options.get("uBiomeOverride",-1)>=0 or options.get("mode",0)!=0:
            for a,b in zip(before["density"],after["density"]):
                largest_error=max(largest_error,abs(a-b))
                assert math.isclose(a,b,rel_tol=2e-5,abs_tol=4e-5), ("Forced biome density changed",index,a,b)
            samples+=len(after["density"])
        elif before["density_raw"]!=after["density_raw"]:
            changed_mixed+=1
    if not parameter_reference and not legacy_band:
        assert changed_mixed>0, "Mixed density still follows the former per-biome blend"
    if legacy_band:
        print(f"PASS biome band migration reference: {len(cases)} cases preserve height/climate/offset/IDs exactly; retuned bands stay within 8.101 m of their former defaults; {samples} finite densities")
        return
    print(f"PASS table refactor reference: {len(cases)} unchanged 2D-column cases; {samples} forced/analytic density samples within tolerance (max error {largest_error:.6g}); {changed_mixed} mixed cases now blend parameters")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-shaders",type=Path,
        help="Saved shader revision: exact unchanged column data, bounded legacy-band migration or tolerant density comparison")
    args=parser.parse_args()
    subprocess.run([str(biome_exporter())],check=True)
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_biome_masks(gl)
        check_volume_noise_budget(gl)
        generic=check_pipeline(gl)
        check_shared_volume(gl,generic)
        if args.reference_shaders:
            check_reference(gl,generic,args.reference_shaders)
        check_free_surface(gl,generic)
        check_volume_table_edit(gl,generic)
        gl.check("generic biome pipeline")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
