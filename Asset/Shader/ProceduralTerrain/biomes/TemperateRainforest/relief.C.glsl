#version 450 core
// Biome 13: TemperateRainforest. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 13u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float valley = noiseFbmTerrain2D(p * 0.0014, uSeed, 230u) * 62.0;
    float tower = noiseLandmark2D01(p, 256.0, uSeed ^ (233u), NOISE_EUCLIDEAN, 0.15, 0.32, 0.75);
    float benches = noiseTerrace(valley, 30.0, 0.65);
    float h = mix(valley, benches, 0.55) + tower * (480.0 + 55.0 * noisePerlin2D(p * 0.001, uSeed ^ (234u)));
    return vec4(h, 506.0, -110.0, tower);
}

#include "../../field_surface_accumulate.glsl"
