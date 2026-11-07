#!/usr/bin/env python3
"""Compare actual generic/specialized fields, classification and editable 3D recipes."""
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from biome_test_support import biome_exporter, biome_masks, biome_defines
from check_gpu import FieldRunner, MeshRunner, SHADERS, COLUMN_BYTES, lattice_values
from gl_headless import GL


def classification_mask(field):
    low = high = 0
    for offset in range(0, len(field["classification_raw"]), COLUMN_BYTES):
        cell = struct.unpack_from("<I", field["classification_raw"], offset + 44)[0]
        assert cell < 64, ("Invalid or unwritten climate cell", cell)
        if cell < 32:
            low |= 1 << cell
        else:
            high |= 1 << (cell - 32)
    assert field["cell_mask"] == (low, high), "GPU reduction omitted a column or halo cell"
    return low, high


def compare(a, b, label):
    assert a["columns_raw"] == b["columns_raw"], label + ": specialized columns differ"
    if a["density_raw"] != b["density_raw"]:
        largest = max(abs(x-y) for x, y in zip(a["density"], b["density"]))
        raise AssertionError(f"{label}: specialized density differs (max {largest})")


def check_variants(gl):
    generic = FieldRunner(gl)
    variants = {}
    cases = (
        dict(cells=8, origin=(-8, -8, -8), seed=1337),
        dict(cells=8, origin=(0, -8, -8), seed=1337),
        dict(cells=8, origin=(-16, -16, -16), seed=1337, lod=1),
        dict(cells=16, origin=(-48, -64, 16), seed=17, uClimateFrequency=.04,
             uHeightAmplitude=240., uHeightFrequency=.03),
        dict(cells=8, origin=(-8, -8, -8), seed=19, uSeaLevel=500.),
    )
    outputs = []
    for index, options in enumerate(cases):
        reference = generic.run(**options)
        cells = classification_mask(reference)
        masks = biome_masks(*cells)
        assert masks[0] and masks[1]
        if masks not in variants:
            variants[masks] = FieldRunner(gl, biome_defines(*masks))
        specialized = variants[masks].run(**options)
        compare(reference, specialized, f"Automatic case {index}")
        outputs.append(specialized)
    fine, neighbor, coarse = map(lattice_values, outputs[:3])
    for other in (neighbor, coarse):
        shared = fine.keys() & other.keys()
        assert shared and all(fine[p] == other[p] for p in shared), "Different chunk variants disagree at shared LOD/halo samples"
    # A variant with only the forced biome must also cover every recipe name.
    # Compile all names together once, then representative minimal variants.
    all_variant = FieldRunner(gl, biome_defines((1<<26)-1, (1<<26)-1))
    for biome in (0, 11, 25):
        masks = (1<<biome, 1<<biome)
        specific = FieldRunner(gl, biome_defines(*masks))
        options = dict(cells=8, origin=(-8, -8, -8), uBiomeOverride=biome,
                       uHeightAmplitude=0., uBandThickness=256., seed=31)
        reference = generic.run(**options)
        compare(reference, specific.run(**options), f"Forced biome {biome}")
        compare(reference, all_variant.run(**options), f"All-defines biome {biome}")
    # Compare actual emitted positions/normals, not just a program's link status.
    options = dict(cells=8, origin=(-8, 0, -8), uBiomeOverride=11,
                   uHeightAmplitude=0., uBandThickness=0., uBandVariation=0., seed=31)
    surface = generic.run(**options)
    options["origin"] = (-8, math.floor(surface["columns"][0]/8)*8, -8)
    ordinary = generic.run(**options)
    specialized = FieldRunner(gl, biome_defines(1<<11,1<<11)).run(**options)
    mesh = MeshRunner(gl)
    a, b = mesh.run(ordinary), mesh.run(specialized)
    assert a["count"] == b["count"] and a["count"] > 0, "Variant mesh did not exercise a surface"
    assert sorted(zip(a["vertices"],a["normals"])) == sorted(zip(b["vertices"],b["normals"])), "Variant changes meshed geometry/normals"
    print(f"PASS variants: {len(variants)} automatic masks, local ratios, halo classification, negative coordinates, shared LOD samples and emitted normals")
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
        after = FieldRunner(gl, biome_defines(1,1),directory).run(**options)
        assert before["columns_raw"] == after["columns_raw"], "Editing a volume recipe changed the 2D preparation"
        assert before["density_raw"] != after["density_raw"], "Custom Voronoi 3D recipe did not change the density"
        assert all(math.isfinite(x) for x in after["density"])
        other_after = FieldRunner(gl,biome_defines(1<<11,1<<11),directory).run(**dict(options,uBiomeOverride=11))
        compare(other_before,other_after,"Unedited biome after polar 3D edit")
    print("PASS free 3D recipe: temporary polar Voronoi code changes actual density, preserves 2D columns and leaves another biome unchanged")


def main():
    subprocess.run([str(biome_exporter())],check=True)
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        generic=check_variants(gl)
        check_free_volume(gl,generic)
        gl.check("biome variants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
