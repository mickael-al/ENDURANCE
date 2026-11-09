#version 450 core
// Biome 16: DrySavanna. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 16u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float pediment = noiseFbmTerrain2D(p * 0.0011, uSeed, 260u) * 60.0;
    float inselberg = noiseLandmark2D01(p, 512.0, uSeed ^ (263u), NOISE_EUCLIDEAN, 0.22, 0.0, 0.55);
    float h = mix(pediment, noiseTerrace(pediment, 32.0, 0.80), 0.35);
    h += inselberg * (470.0 + 55.0 * noisePerlin2D(p * 0.001, uSeed ^ (264u)));
    return vec4(h, 386.0, -72.0, inselberg);
}

#include "../../field_surface_accumulate.glsl"
