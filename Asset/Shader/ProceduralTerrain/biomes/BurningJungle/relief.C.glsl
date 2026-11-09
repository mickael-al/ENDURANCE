#version 450 core
// Biome 23: BurningJungle. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 23u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(p, vec2(0.8, -0.6));
    float flow = noiseFbmTerrain2D(q * vec2(0.0008, 0.0028), uSeed, 330u) * 76.0;
    float basalt = noiseLandmark2D01(p, 128.0, uSeed ^ (333u), NOISE_CHEBYSHEV, 0.15, 0.40, 0.82);
    float h = noiseTerrace(flow, 32.0, 0.62);
    h += basalt * (520.0 + 55.0 * noisePerlin2D(p * 0.0015, uSeed ^ (334u)));
    return vec4(h, 526.0, -108.0, basalt);
}

#include "../../field_surface_accumulate.glsl"
