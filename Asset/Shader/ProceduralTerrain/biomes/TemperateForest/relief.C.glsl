#version 450 core
// Biome 12: TemperateForest. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 12u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float hills = noiseFbmTerrain2D(p * 0.0016, uSeed, 220u) * 115.0;
    float tor = noiseLandmark2D01(p, 384.0, uSeed ^ (223u), NOISE_EUCLIDEAN, 0.22, 0.0, 0.65);
    float h = hills + tor * (270.0 + 45.0 * noisePerlin2D(p * 0.001, uSeed ^ (224u)));
    return vec4(h, 426.0, -80.0, tor);
}

#include "../../field_surface_accumulate.glsl"
