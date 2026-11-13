#!/usr/bin/env python3
"""Measure the current polar relief and independent volume field in headless GL.

The optional preview shows sampled geometry, not textures or generated artwork.
Readbacks are diagnostic only and never enter the runtime terrain path.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import random
import struct
import sys

from check_biomes import BiomeProbe
from check_gpu import FieldRunner, LegacyFieldRunner, SHADERS, lattice_values
from gl_headless import GL, GLUnavailable, shader_source


PROBE = r"""
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform uint uProbeMode;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
uniform float uProbeStepY;
void main()
{
    uvec2 index=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(index,uvec2(uProbeSize)))) return;
    vec2 p=uProbeOrigin+vec2(index)*uProbeSpacing;
    FieldColumn column=fieldProbeColumn(p);
    vec4 diagnosticVolume=fieldProbeVolume(column);
    float h=column.terrain.x;
    vec4 samples;
    if(uProbeMode==1u)
    {
        float e=uProbeStepY;
        // This mode isolates the polar surface with macro amplitude zero.
        // This program contains only PolarIce's production recipe pair.
        samples=uBaseHeight+uDetailScale*vec4(
                     fieldClampRelief(fieldRelief(p+vec2(e,0))).x,
                     fieldClampRelief(fieldRelief(p-vec2(e,0))).x,
                     fieldClampRelief(fieldRelief(p+vec2(0,e))).x,
                     fieldClampRelief(fieldRelief(p-vec2(0,e))).x);
    }
    else
    {
        samples=vec4(fieldProbeDensity(vec3(p.x,h+8.0,p.y),column),
                     fieldProbeDensity(vec3(p.x,h+24.0,p.y),column),
                     fieldProbeDensity(vec3(p.x,h-16.0,p.y),column),
                     fieldProbeDensity(vec3(p.x,h-40.0,p.y),column));
    }
    outputValues[index.x+index.y*uProbeSize]=
        ProbeSample(column.terrain,diagnosticVolume,samples,vec4(0.0));
}
"""

class PolarProbe(BiomeProbe):
    def __init__(self, gl, directory=SHADERS, source=None):
        super().__init__(gl, directory=directory, source=source or PROBE)


def check_voronoi(gl):
    # A wider independent search checks both nearest sites, including negative
    # coordinates and positions close to search-cell boundaries. In particular,
    # an F1-only implementation accidentally reused as F2 cannot pass this.
    randomizer = random.Random(137)
    positions = [(randomizer.uniform(-32, 32), randomizer.uniform(-32, 32))
                 for _ in range(1024)]
    positions += [(x+e, z-e) for x in range(-5, 6) for z in range(-5, 6)
                  for e in (-.0001, .0001)]
    raw = struct.pack(f"<{len(positions)*2}f", *(v for p in positions for v in p))
    floats = struct.unpack(f"<{len(positions)*2}f", raw)
    positions = list(zip(floats[::2], floats[1::2]))
    source = "#version 450 core\nuniform uint uSeed;\n" + shader_source(SHADERS / "../Noise/noise_2d.glsl") + r"""
layout(local_size_x=64) in;
layout(std430,binding=7) readonly buffer ProbeInput { vec2 points[]; };
layout(std430,binding=6) writeonly buffer ProbeOutput { vec2 distances[]; };
uniform uint uCount;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i<uCount) distances[i]=noiseCellularDistancesSquared2D(points[i], uSeed ^ (102u));
}
"""
    program = gl.compile([(0x91B9, source)], "polar Voronoi distances")
    gl.buffer(raw, 7)
    output = gl.buffer(len(raw), 6)
    gl.uniform(program, "uCount", "1ui", len(positions))
    gl.uniform(program, "uSeed", "1ui", 1337)
    gl.dispatch_extent(program, len(positions))
    actual = struct.unpack(f"<{len(positions)*2}f", gl.read(output, len(raw)))

    def hash32(value):
        value &= 0xffffffff
        value ^= value >> 16
        value = (value*0x7feb352d) & 0xffffffff
        value ^= value >> 15
        value = (value*0x846ca68b) & 0xffffffff
        return value ^ (value >> 16)

    largest = 0.
    for index, (x, z) in enumerate(positions):
        cx, cz = math.floor(x), math.floor(z)
        reference = []
        for dz in range(-3, 4):
            for dx in range(-3, 4):
                h = hash32(((cx+dx)*0x9e3779b9) ^ ((cz+dz)*0x85ebca6b) ^ 1337 ^ 102)
                sx = cx+dx+.35+.30*(h & 65535)/65535
                sz = cz+dz+.35+.30*(h >> 16)/65535
                reference.append((sx-x)**2 + (sz-z)**2)
        reference.sort()
        a, b = actual[index*2:index*2+2]
        assert 0 <= a <= b, ("Invalid ordered Voronoi distances", x, z, a, b)
        largest = max(largest, abs(a-reference[0]), abs(b-reference[1]))
    assert largest < 2e-6, ("3x3 Voronoi misses a nearest or second-nearest feature", largest)
    print(f"PASS polar Voronoi: {len(positions)} F1/F2 samples match independent 7x7 search; error {largest:.3g}")


def check_repeat_and_lod(gl):
    fields = FieldRunner(gl)
    options = {"uBiomeOverride": 0, "seed": 1337}
    first = fields.run(cells=16, origin=(-16, -16, -16), **options)
    repeated = fields.run(cells=16, origin=(-16, -16, -16), **options)
    assert first["density_raw"] == repeated["density_raw"], "Polar seed is not deterministic"
    assert first["columns_raw"] == repeated["columns_raw"], "Polar columns are not deterministic"
    coarse = fields.run(cells=16, origin=(-32, -32, -32), lod=1, **options)
    neighbor = fields.run(cells=16, origin=(0, -16, -16), **options)
    fine_values = lattice_values(first)
    count = 0
    for sample in (coarse, neighbor):
        other = lattice_values(sample)
        shared = fine_values.keys() & other.keys()
        assert len(shared) >= 125
        assert all(fine_values[p] == other[p] for p in shared), "Polar shared samples differ by chunk or LOD"
        count += len(shared)
    print(f"PASS polar field: repeatable seed and {count} bitwise shared chunk/LOD samples")


def check_other_biomes(gl, directory):
    directory=Path(directory)
    reference_runner=FieldRunner if (directory/"field_batch_classify.C.glsl").is_file() else LegacyFieldRunner
    old,new=reference_runner(gl,directory=directory),FieldRunner(gl)
    for biome in (25,):
        for seed in (1337,98765):
            options=dict(cells=8,origin=(-40,-12,24),lod=3,seed=seed,
                         uBaseSpacing=4.,uBiomeOverride=biome,uHeightAmplitude=0.,
                         uDetailScale=.35)
            before,after=old.run(**options),new.run(**options)
            # New production columns occupy the former reserved surface-ID
            # word. Compare numeric channels and volume metadata separately.
            assert all(math.isclose(a,b,rel_tol=2e-5,abs_tol=4e-5)
                       for i,(a,b) in enumerate(zip(before["columns"],after["columns"]))
                       if i%12<8),("Unrelated numeric columns changed",biome,seed)
            assert all(before["columns_raw"][i+36:i+48]==after["columns_raw"][i+36:i+48]
                       for i in range(0,len(before["columns_raw"]),48)),("Unrelated volume IDs changed",biome,seed)
            assert all(math.isclose(a,b,rel_tol=2e-5,abs_tol=4e-5)
                       for a,b in zip(before["density"],after["density"])), ("Unrelated density changed",biome,seed)
    print("PASS scope: Marine retains its numeric columns, volume IDs and densities within tolerance")


def check_relief(probe):
    sample = probe.run(biome=0, size=257, origin=(-253.25, -249.75), spacing=2.,
                       uHeightAmplitude=0., mode=1, step_y=.002)
    values, size = sample["values"], sample["size"]
    heights = values[0::16]
    # Report geometry without imposing the obsolete basin recipe or sparse
    # activity mask: these are now deliberately user-controlled settings.
    minima = 0
    slopes = []
    for z in range(1, size-1):
        for x in range(1, size-1):
            i = z*size+x
            neighbours = (heights[i-1], heights[i+1], heights[i-size], heights[i+size])
            if all(heights[i] < h for h in neighbours):
                minima += 1
            slopes.append(math.hypot(heights[i+1]-heights[i-1],
                                     heights[i+size]-heights[i-size]) / 4.)
    span = max(heights)-min(heights)
    assert span > .01 and all(math.isfinite(h) for h in heights), ("Invalid polar relief",span)
    gentle = sum(s < math.tan(math.radians(30)) for s in slopes) / len(slopes)
    steep = sum(s > math.tan(math.radians(45)) for s in slopes) / len(slopes)
    epsilon_jump = max(abs(values[i*16+j]-heights[i])
                       for i in range(size*size) for j in range(8, 12))
    assert epsilon_jump < .1, ("Polar relief has a discontinuous local jump", epsilon_jump)
    assert all(-1910.401 <= h <= 1910.401 for h in heights)
    print(f"PASS polar relief: {minima} local minima in 512x512 m, "
          f"height span {span:.3f} m, {gentle:.2%} of slopes below30deg, "
          f"{steep:.2%} above45deg, "
          f"largest 2mm height difference {epsilon_jump:.6g} m")
    return sample


def check_volume(probe):
    changed = [0]*4
    carved = [0]*4
    total = 0
    maximum = [0.]*4
    for seed in (1337, 1, 17, 98765):
        sample = probe.run(biome=0, size=129, origin=(-1047.25, -1011.75),
                           spacing=16., uSeed=seed, uHeightAmplitude=0.)
        values = sample["values"]
        for i in range(sample["size"]**2):
            h, band, offset = values[16*i], values[16*i+3], values[16*i+4]
            assert min(h, h+offset-.5*band) >= -1910.401
            assert max(h, h+offset+.5*band) <= 1910.401
            total += 1
            for j, base in enumerate((-8., -24., 16., 40.)):
                density = values[16*i+8+j]
                delta = abs(density-base)
                maximum[j] = max(maximum[j], delta)
                changed[j] += delta > .01
                carved[j] += density < 0
    affected = [n/total for n in changed]
    holes = [n/total for n in carved]
    assert sum(changed) > 0, "Polar 3D detail was disabled entirely"
    print(f"PASS bounded polar volume at offsets+8/+24/-16/-40m: "
          f"affected fractions {affected}; air fractions {holes}; "
          f"maximum density changes {maximum}")


def check_scene_height(probe):
    # The scene uses a larger macro amplitude than the class defaults. Probe a
    # wide world region instead of inferring its extrema from a chunk's corners.
    sample = probe.run(biome=0, size=65, origin=(-12000., -12000.), spacing=375.,
                       uHeightAmplitude=1000., uHeightFrequency=.000015)
    values = sample["values"]
    bound = 1000.+4096.*.35+.5*(8192.*.35)+160.*.35
    for i in range(sample["size"]**2):
        height, band, offset = values[i*16], values[i*16+3], values[i*16+4]
        assert min(height, height+offset-band*.5) >= -bound
        assert max(height, height+offset+band*.5) <= bound
    print("PASS polar scene amplitude1000: sampled heights and displaced bands remain within analytic bounds")


def write_preview(sample, destination):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource
    destination.parent.mkdir(parents=True, exist_ok=True)
    size, spacing = sample["size"], sample["spacing"]
    heights = np.asarray(sample["values"]).reshape(size, size, 16)[:, :, 0]
    coordinates = np.arange(size)*spacing
    x, z = np.meshgrid(coordinates, coordinates)
    hill = LightSource(azdeg=305, altdeg=36)
    colors = hill.shade(heights, cmap=plt.get_cmap("Blues_r"), vert_exag=1., dx=spacing,
                       dy=spacing, blend_mode="soft")
    figure = plt.figure(figsize=(13, 6), layout="constrained")
    above = figure.add_subplot(121)
    above.imshow(colors, origin="lower", extent=(0, coordinates[-1], 0, coordinates[-1]))
    above.set(xlabel="X (m)", ylabel="Z (m)", title="Relief 2D calculé par les shaders")
    perspective = figure.add_subplot(122, projection="3d")
    step = 2
    perspective.plot_surface(x[::step, ::step], z[::step, ::step], heights[::step, ::step],
                             facecolors=colors[::step, ::step], linewidth=0, antialiased=True,
                             shade=False, rstride=1, cstride=1)
    perspective.view_init(elev=23, azim=-58)
    perspective.set(xlabel="X (m)", ylabel="Z (m)", zlabel="H (m)", title="Vue géométrique — sans textures")
    perspective.set_box_aspect((1, 1, max(.04, float(np.ptp(heights))/coordinates[-1])))
    figure.suptitle("Glace polaire — seed1337, relief du biome isolé, amplitude macro nulle")
    figure.savefig(destination, dpi=160)
    plt.close(figure)
    print(f"WROTE sampled polar relief preview: {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-shaders", type=Path,
                        help="Optional baseline directory to verify the untouched Marine recipe")
    parser.add_argument("--preview", type=Path,
                        help="Optional measured relief image (requires numpy/matplotlib)")
    args = parser.parse_args()
    try:
        with GL() as gl:
            print(f"OpenGL {gl.version}; renderer: {gl.renderer}")
            check_voronoi(gl)
            probe = PolarProbe(gl)
            relief = check_relief(probe)
            check_volume(probe)
            check_scene_height(probe)
            check_repeat_and_lod(gl)
            if args.reference_shaders:
                check_other_biomes(gl, args.reference_shaders)
            if args.preview:
                write_preview(relief, args.preview)
    except GLUnavailable as error:
        print(f"SKIP: {error}", file=sys.stderr)
        return 77
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
