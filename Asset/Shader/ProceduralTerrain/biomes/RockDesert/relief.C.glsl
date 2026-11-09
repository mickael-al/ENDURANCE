#version 450 core
// Biome 15: RockDesert. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 15u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float floorWaves = noiseDunes2D01(p, vec2(0.8, 0.6), 420.0, uSeed ^ (250u)) * 62.0;
    float mesa = noiseLandmark2D01(p, 640.0, uSeed ^ (251u), NOISE_EUCLIDEAN, 0.22, 0.58, 0.64);
    float layers = noiseTerrace(mesa * 570.0, 54.0, 0.42);
    float h = floorWaves + layers + noisePerlin2D(p * 0.002, uSeed ^ (252u)) * 8.0;
    return vec4(h, 506.0, -90.0, mesa);
}

#include "../../field_surface_accumulate.glsl"
