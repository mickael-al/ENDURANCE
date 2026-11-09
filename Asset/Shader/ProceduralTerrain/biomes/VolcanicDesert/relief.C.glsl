#version 450 core
// Biome 20: VolcanicDesert. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 20u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float d = noiseCellularDistance2D(p / 768.0, uSeed ^ (300u), NOISE_EUCLIDEAN);
    float shield = noiseMound01(d, 0.43);
    float caldera = 1.0 - smoothstep(0.055, 0.12, d);
    float flow = noisePerlin2D(p * vec2(0.0015, 0.005), uSeed ^ (301u)) * 30.0;
    float basalt = noiseLandmark2D01(p, 512.0, uSeed ^ (302u), NOISE_CHEBYSHEV, 0.14, 0.55, 0.60);
    float h = noiseTerrace(shield * 460.0 - caldera * 240.0 + flow, 28.0, 0.70);
    h += basalt * 110.0;
    return vec4(h, 526.0, -120.0, max(shield, basalt));
}

#include "../../field_surface_accumulate.glsl"
