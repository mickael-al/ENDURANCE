#version 450 core
// Biome 17: Savanna. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 17u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(p, vec2(0.6, -0.8));
    float floorWaves = noiseFbmTerrain2D(q * vec2(0.0009, 0.0016), uSeed, 270u) * 85.0;
    float kopje = noiseLandmark2D01(p, 384.0, uSeed ^ (273u), NOISE_EUCLIDEAN, 0.20, 0.0, 0.60);
    float h = floorWaves + kopje * (330.0 + 45.0 * noisePerlin2D(p * 0.0015, uSeed ^ (274u)));
    return vec4(h, 390.0, -74.0, kopje);
}

#include "../../field_surface_accumulate.glsl"
