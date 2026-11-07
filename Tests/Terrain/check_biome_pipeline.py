#!/usr/bin/env python3
"""Verify the generic biome pipeline, packed CPU matrices and editable 3D recipes."""
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

from biome_test_support import biome_exporter
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
                     uHeightAmplitude=0.,uBandThickness=256.,seed=31)
        field=generic.run(**options)
        assert finite_columns(field["columns"]) and all(math.isfinite(x) for x in field["density"])
        compare(field,generic.run(**options),f"Repeated biome {biome}")
    options=dict(cells=8,origin=(-8,0,-8),uBiomeOverride=11,
                 uHeightAmplitude=0.,uBandThickness=0.,uBandVariation=0.,seed=31)
    surface=generic.run(**options)
    options["origin"]=(-8,math.floor(surface["columns"][0]/8)*8,-8)
    mesh=MeshRunner(gl)
    first=mesh.run(generic.run(**options));second=mesh.run(generic.run(**options))
    assert first["count"]==second["count"] and first["count"]>0, "Mesh fixture missed the surface"
    assert sorted(zip(first["vertices"],first["normals"]))==sorted(zip(second["vertices"],second["normals"])), "Repeated mesh differs"
    print("PASS generic biome pipeline: 26 recipes, deterministic fields/meshes, local ratios, negative coordinates and shared LOD/halo samples")
    return generic


def check_free_volume(gl, generic):
    options = dict(cells=8, origin=(-8,-8,-8), seed=1337, uBiomeOverride=0,
                   uHeightAmplitude=0., uBandThickness=256., uBandVariation=0.)
    before = generic.run(**options)
    other_before = generic.run(**dict(options,uBiomeOverride=11))
    with tempfile.TemporaryDirectory(prefix="terrain-free-volume-") as temporary:
        directory = Path(temporary)/"shaders"
        shutil.copytree(SHADERS, directory)
        path = directory/"biome_volume.glsl"
        source = path.read_text()
        start = source.index("float fieldVolumePolarIce(")
        body = source.index("{", start)
        # A genuinely different 3D algorithm, with a bounded envelope, proves
        # that the biome function controls density beyond parameter selection.
        injected = """
    float customBase=column.terrain.x-p.y;
    float customBand=max(column.terrain.w*0.5,0.001);
    float customWeight=1.0-smoothstep(0.0,customBand,abs(customBase));
    return customBase+customWeight*(fieldCellular3(p*0.03125,713u)-0.5)*24.0;
"""
        path.write_text(source[:body+1] + injected + source[body+1:])
        after = FieldRunner(gl,directory=directory).run(**options)
        assert before["columns_raw"] == after["columns_raw"], "Editing a volume recipe changed the 2D preparation"
        assert before["density_raw"] != after["density_raw"], "Custom Voronoi 3D recipe did not change the density"
        assert all(math.isfinite(x) for x in after["density"])
        other_after = FieldRunner(gl,directory=directory).run(**dict(options,uBiomeOverride=11))
        compare(other_before,other_after,"Unedited biome after polar 3D edit")
    print("PASS free 3D recipe: temporary polar Voronoi code changes actual density, preserves 2D columns and leaves another biome unchanged")


def main():
    subprocess.run([str(biome_exporter())],check=True)
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        generic=check_pipeline(gl)
        check_free_volume(gl,generic)
        gl.check("generic biome pipeline")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
