#version 450 core
// Biome 8: ColdForest. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 8u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(p, vec2(0.6, 0.8));
    float valley = 1.0 - smoothstep(0.05, 0.28,
        abs(noisePerlin2D(q * vec2(0.00055, 0.0017), uSeed ^ (180u))));
    float buttress = noiseLandmark2D01(p, 384.0, uSeed ^ (181u), NOISE_MANHATTAN, 0.22, 0.12, 0.68);
    float h = (1.0 - valley) * 145.0 - valley * 120.0;
    h += noisePerlin2D(p * 0.0025, uSeed ^ (182u)) * 35.0 + buttress * 360.0;
    return vec4(h, 446.0, -96.0, buttress);
}

#include "../../field_surface_accumulate.glsl"
