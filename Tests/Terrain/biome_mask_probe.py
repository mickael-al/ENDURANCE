"""Exercise production biome dispatch with inexpensive, instrumented recipes.

Only recipe bodies are replaced in memory. Production mask construction,
switch dispatch, weight aggregation and column preparation remain untouched.
"""
import math
import re
import struct

from check_gpu import SHADERS
from gl_headless import shader_source


RELIEF_NAMES = """
PolarIce FrozenTundra Tundra Taiga BorealWetland RockyTundra ColdSteppe
BorealForest ColdForest ColdMarsh DryPlain Prairie TemperateForest
HumidTemperateForest TemperateMarsh RockDesert DrySavanna Savanna DryJungle
TropicalJungle VolcanicDesert RedDesert AridPlateau BurningJungle
TropicalMangrove Marine
""".split()
VOLUME_NAMES = """
PolarIce FrozenTundra Tundra Taiga BorealBog RockyTundra ColdSteppe
BorealForest ColdForest ColdMarsh DryPlain Meadow TemperateForest
TemperateRainforest TemperateMarsh RockDesert DrySavanna Savanna DryJungle
TropicalJungle VolcanicDesert RedDesert AridPlateau BurningJungle
TropicalMangrove Marine
""".split()


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
layout(std430,binding=9) readonly buffer ProbeInputs {
    uvec4 probeSurfaceIDs;
    vec4 probeSurfaceRatios;
    vec4 probeBlend; // marine, local humidity ratio, temperature ratio, altitude
};
layout(std430,binding=10) buffer ProbeResults {
    vec4 probeSurface;
    float probeDensity;
    uint probePreparedMask;
    uint probeSurfaceCalls[26];
    uint probeVolumeCalls[26];
};
void main()
{
    for(uint i=0u;i<26u;++i) { surfaceCalls[i]=0u; volumeCalls[i]=0u; }
    FieldColumn raw;
    raw.terrain=vec4(10.0,0.5,0.5,0.0);
    raw.blend=vec4(probeBlend.yzw,0.0);
    raw.detail=uvec4(0u);
    FieldColumn column=fieldColumnFromClimate(vec2(0.0),raw);
    probePreparedMask=column.detail.w;

    // Isolate dispatcher calls from the separate column-preparation check.
    for(uint i=0u;i<26u;++i) { surfaceCalls[i]=0u; volumeCalls[i]=0u; }
    probeSurface=fieldSurfaceRecipes(vec2(0.0),probeSurfaceIDs,
        probeSurfaceRatios,probeBlend.x);
    column.terrain=vec4(10.0,0.5,0.5,256.0);
    column.blend=vec4(probeBlend.w,0.0,0.0,1.0);
    probeDensity=fieldDensityAtPrepared(vec3(0.0,8.0,0.0),column,probeBlend.yz);
    for(uint i=0u;i<26u;++i) {
        probeSurfaceCalls[i]=surfaceCalls[i];
        probeVolumeCalls[i]=volumeCalls[i];
    }
}
"""


def check_biome_masks(gl):
    source = shader_source(SHADERS / "world_field.glsl")
    for biome, name in enumerate(RELIEF_NAMES):
        source = replace_body(source, "fieldRelief" + name,
            f"++surfaceCalls[{biome}]; return vec4({biome}.0-13.0,"
            f"{biome}.0+2.0,{biome}.0*0.5,0.5);")
    for biome, name in enumerate(VOLUME_NAMES):
        source = replace_body(source, "fieldVolume" + name,
            f"++volumeCalls[{biome}]; return column.terrain.x-p.y+{biome+1}.0;")
    source = "#version 450 core\nuint surfaceCalls[26];\nuint volumeCalls[26];\n" + source + PROBE
    program = gl.compile([(0x91B9, source)], "sparse biome mask probe")
    inputs = gl.buffer(48, 9)
    output = gl.buffer(232, 10)
    mapping = gl.buffer(64 * 16, 7)
    gl.uniform(program, "uFieldMode", "1ui", 0)
    gl.uniform(program, "uDetailScale", "1f", 1.0)
    gl.uniform(program, "uBandThickness", "1f", 256.0)

    # Four surface corners plus sea; eight genuinely different volume corners.
    # The latter cannot be covered by the current repeated production layers.
    cases = [
        ((0, 4, 18, 24), (.125, .375, .125, .375), tuple(range(8)), (.25, .25, .75, .5), -1),
        ((0, 0, 25, 25), (.25,) * 4, (0, 0, 25, 25) * 2, (.25, .5, .5, .5), -1),
        ((1, 2, 3, 4), (1., 0., 0., 0.), tuple(range(1, 9)), (0., 0., 0., 0.), -1),
        ((0, 7, 13, 24), (.25,) * 4, (25,) * 8, (1., .17, .43, .73), -1),
        ((25,) * 4, (.25,) * 4, (25,) * 8, (0., .25, .75, .5), -1),
        ((7,) * 4, (.25,) * 4, (7,) * 8, (0., .25, .75, .5), -1),
        ((0, 1, 2, 3), (0.,) * 4, tuple(range(8)), (0., 1., 1., 1.), -1),
    ]
    cases += [((i,) * 4, (1., 0., 0., 0.), tuple(range(8)), (0., .25, .75, .5), i)
              for i in range(26)]
    for case, (surface_ids, ratios, volume_ids, blend, override) in enumerate(cases):
        mask = sum(1 << biome for biome in set(volume_ids))
        encoded_surface = sum(biome << (5 * corner) for corner, biome in enumerate(surface_ids))
        low = sum(biome << (5 * corner) for corner, biome in enumerate(volume_ids[:6]))
        high = sum(biome << (5 * corner) for corner, biome in enumerate(volume_ids[6:]))
        gl.write(mapping, struct.pack("<4I", encoded_surface, low, high, mask) * 64)
        gl.write(inputs, struct.pack("<4I8f", *surface_ids, *ratios, *blend))
        gl.uniform(program, "uBiomeOverride", "1i", override)
        gl.dispatch(program, 1)
        result = struct.unpack("<5f53I", gl.read(output, 232))
        assert result[5] == (mask if override < 0 else 1 << override), ("Prepared volume mask", case)

        # Independent CPU oracle: accumulate each corner, not the shader's mask.
        marine, fx, fy, altitude = blend
        surface_weights, volume_weights = [0.] * 26, [0.] * 26
        for biome, weight in zip(surface_ids, ratios):
            surface_weights[biome] += weight * (1. - marine)
        surface_weights[25] += marine
        for corner, biome in enumerate(volume_ids):
            volume_weights[biome] += ((fx if corner & 1 else 1. - fx)
                                     * (fy if corner & 2 else 1. - fy)
                                     * (altitude if corner & 4 else 1. - altitude))
        if override >= 0:
            volume_weights = [float(i == override) for i in range(26)]
        expected_surface = [sum(w * (i - 13., i + 2., i * .5, .5)[component]
                                for i, w in enumerate(surface_weights))
                            for component in range(4)]
        expected_density = 2. + sum(w * (i + 1) for i, w in enumerate(volume_weights))
        assert all(math.isclose(a, b, rel_tol=1e-6, abs_tol=3e-5)
                   for a, b in zip(result[:5], expected_surface + [expected_density])), ("Local blend", case, result[:5])
        assert result[6:32] == tuple(int(w > 0.) for w in surface_weights), ("Surface recipe call count", case)
        assert result[32:58] == tuple(int(w > 0.) for w in volume_weights), ("Volume recipe call count", case)
    gl.check("biome mask probe")
    print("PASS sparse biome masks: 33 cases, five surface/eight volume contributors, duplicates, zero weights, sea and all forced IDs; each active recipe called once")
