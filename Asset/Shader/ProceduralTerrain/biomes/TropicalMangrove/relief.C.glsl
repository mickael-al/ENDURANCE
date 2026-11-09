#version 450 core
// Biome 24: TropicalMangrove. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 24u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float tide = noisePerlin2D(p * 0.0011, uSeed ^ (340u)) * 20.0;
    float channel = 1.0 - smoothstep(0.025, 0.12,
        abs(noisePerlin2D(p * vec2(0.0024, 0.0046), uSeed ^ (341u))));
    float island = noiseLandmark2D01(p, 384.0, uSeed ^ (342u), NOISE_EUCLIDEAN, 0.21, 0.0, 0.55);
    float h = noiseTerrace(tide, 6.0, 0.88) - channel * 25.0 + island * 100.0;
    return vec4(h, 310.0, -52.0, island);
}

#include "../../field_surface_accumulate.glsl"
