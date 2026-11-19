"""Check production local weights/masks and the optional volume-helper budget.

Weights are compared with independent CPU corner sums. Each biome's production
compute program is exercised by the batch/recipe tests; no combined recipe
switch or interpolation of volume parameters is reconstructed here.
"""
import math
import re
import struct

from biome_test_support import BIOME_NAMES
from check_gpu import SHADERS
from gl_headless import shader_source


RELIEF_NAMES = BIOME_NAMES


def replace_body(source, name, body):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", source)
    assert match, "Missing biome recipe: " + name
    start = match.end() - 1
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[:start] + "{\n" + body + "\n}" + source[end:]


PROBE = r"""
layout(local_size_x=1) in;
layout(std430,binding=9) readonly buffer ProbeInputs { FieldColumn probeColumn; };
layout(std430,binding=10) buffer ProbeResults {
    float surfaceWeights[26];
    float volumeWeights[26];
    uint surfaceMask;
    uint volumeMask;
};
void main()
{
    for(uint biome=0u;biome<26u;++biome)
    {
        surfaceWeights[biome]=fieldSurfaceWeight(probeColumn,biome);
        volumeWeights[biome]=fieldVolumeWeight(probeColumn,biome);
    }
    surfaceMask=fieldSurfaceMask(probeColumn);
    volumeMask=fieldVolumeMask(probeColumn);
}
"""


def check_biome_masks(gl):
    source = "#version 450 core\n" + shader_source(SHADERS / "field_weights.glsl") + PROBE
    program = gl.compile([(0x91B9, source)], "production biome weights and masks")
    inputs = gl.buffer(48, 9)
    output = gl.buffer(216, 10)
    # Real local weights share the climate ratios; sea mixing is the lowest
    # altitude cell's complement. Eight independent volume corners also cover
    # table configurations that today's repeated altitude layers do not use.
    cases = [
        ((0, 4, 18, 24), tuple(range(8)), (.25, .75), .5, 0, -1),
        ((0, 0, 25, 25), (0, 0, 25, 25) * 2, (.5, .5), .5, 0, -1),
        ((1, 2, 3, 4), tuple(range(1, 9)), (0., 0.), 0., 16, -1),
        ((0, 7, 13, 24), (25,) * 8, (.17, .43), 0., 0, -1),
        ((25,) * 4, (25,) * 8, (.25, .75), .5, 16, -1),
        ((7,) * 4, (7,) * 8, (.25, .75), .5, 16, -1),
        ((0, 7, 13, 24), tuple(range(8)), (.01, .99), 1e-6, 16, -1),
    ]
    cases += [((i,) * 4, tuple(range(8)), (.25, .75), .5, 0, i)
              for i in range(26)]
    for case, (surface_ids, volume_ids, fractions, altitude, cell, override) in enumerate(cases):
        encoded_surface = sum(biome << (5 * corner) for corner, biome in enumerate(surface_ids))
        low = sum(biome << (5 * corner) for corner, biome in enumerate(volume_ids[:6]))
        high = sum(biome << (5 * corner) for corner, biome in enumerate(volume_ids[6:]))
        gl.write(inputs, struct.pack("<8f4I", 10., fractions[0] * .25, fractions[1] * .25, 256.,
                                    altitude, 0., 0., 0., encoded_surface | (cell << 20), low, high, 0))
        gl.uniform(program, "uBiomeOverride", "1i", override)
        gl.dispatch(program, 1)
        result = struct.unpack("<52f2I", gl.read(output, 216))

        # Independent CPU oracle: accumulate corners, including repeated IDs.
        def fade(value):
            return value ** 3 * (value * (value * 6. - 15.) + 10.)

        fx, fy = map(fade, fractions)
        marine = 1. - altitude if cell < 16 else 0.
        surface_weights, volume_weights = [0.] * 26, [0.] * 26
        for corner, biome in enumerate(surface_ids):
            weight = (fx if corner & 1 else 1. - fx) * (fy if corner & 2 else 1. - fy)
            surface_weights[biome] += weight * (1. - marine)
        surface_weights[25] += marine
        for corner, biome in enumerate(volume_ids):
            volume_weights[biome] += ((fx if corner & 1 else 1. - fx)
                                     * (fy if corner & 2 else 1. - fy)
                                     * (altitude if corner & 4 else 1. - altitude))
        if override >= 0:
            surface_weights = volume_weights = [float(i == override) for i in range(26)]
        expected = surface_weights + volume_weights
        assert all(math.isclose(a, b, rel_tol=2e-6, abs_tol=3e-6)
                   for a, b in zip(result[:52], expected)), ("Local corner weights", case, result[:52], expected)
        masks = tuple(sum(1 << i for i, weight in enumerate(weights) if weight > 0.)
                      for weights in (surface_weights, volume_weights))
        assert result[52:] == masks, ("Positive local contributor mask", case, result[52:], masks)
        assert all(math.isclose(sum(result[start:start+26]), 1., abs_tol=3e-6)
                   for start in (0, 26)), ("Unnormalized local weights", case)
        if case == 6:
            assert all(result[26+i] > 0. for i in range(8)), "A positive tiny contribution was dropped"
    gl.check("biome weight/mask probe")
    print("PASS production local weights/masks: 33 cases, five surface/eight volume contributors, duplicate IDs, zero and tiny positive weights, sea and every forced biome; CPU corner sums agree")


def check_volume_noise_budget(gl):
    """Inspect one default helper's noise calls, not the whole accumulated field."""
    source = (shader_source(SHADERS / "field_common.glsl")
              + shader_source(SHADERS / "biome_volume_common.glsl"))
    match = re.search(r"float\s+noisePerlin3D\s*\([^)]*\)\s*\{", source)
    assert match
    source = source[:match.end()] + "\n++noiseCalls;\n" + source[match.end():]
    probe = r"""
layout(local_size_x=1) in;
layout(std430,binding=10) buffer NoiseProbeOutput { float densityValue; uint calls; };
uniform vec3 probePoint;
uniform vec2 probeTerrain;
uniform vec4 probeVolume;
void main()
{
    noiseCalls=0u;
    densityValue=fieldRecipeDensityAtPrepared(probePoint,probeTerrain,probeVolume,
        vec4(.35,.50,.15,.10),vec4(.85,.70,.15,.30));
    calls=noiseCalls;
}
"""
    program = gl.compile([(0x91B9, "#version 450 core\nuint noiseCalls;\n" + source + probe)],
                         "common volume noise budget")
    output = gl.buffer(8, 10)
    gl.uniform(program, "uSeed", "1ui", 1337)
    gl.uniform(program, "uBaseHeight", "1f", 10.)
    gl.uniform(program, "uSphereCenter", "3f", 0., 0., 0.)
    gl.uniform(program, "uSphereRadius", "1f", 20.)
    cases = [
        # mode, half-band, pointY, displacement, carving, expected noise calls
        (0, 128., -12., 3., 72., 3),
        (0, 128., -12., 3., 0., 3),
        (0, 128., -12., 0., 72., 3),
        (0, 128., -12., 0., 0., 0),
        (0, 128., 1000., 3., 72., 0),
        (0, 128., 128., 3., 72., 0),
        (0, 0., 0., 3., 72., 0),
        (1, 128., -12., 3., 72., 0),
        (2, 128., -12., 3., 72., 0),
    ]
    for mode, half_band, y, displacement, carving, expected in cases:
        gl.uniform(program, "uFieldMode", "1ui", mode)
        gl.uniform(program, "probePoint", "3f", 8., y, 20.)
        gl.uniform(program, "probeTerrain", "2f", 10., half_band)
        gl.uniform(program, "probeVolume", "4f", 0., displacement, carving, 20.)
        gl.dispatch(program, 1)
        density, count = struct.unpack("<fI", gl.read(output, 8))
        assert count == expected, ("Volume noise budget", mode, half_band, y, displacement, carving, count)
        assert math.isfinite(density)
        if expected == 0:
            target = 20. - math.sqrt(8. ** 2 + y * y + 20. ** 2) if mode == 2 else 10. - y
            assert math.isclose(density, target, rel_tol=1e-6, abs_tol=1e-5), ("Rejected volume changed density", density, target)
    gl.check("volume noise budget")
    print("PASS default recipe-helper noise calls: three inside one active band; zero outside, for disabled volumes and analytical modes (not a whole-field budget)")
