#version 450 core
// Biome 25: Marine. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 25u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float shelf = noiseTerrace(noisePerlin2D(p * 0.0007, uSeed ^ (350u)) * 85.0, 42.0, 0.80);
    float canyon = 1.0 - smoothstep(0.035, 0.20,
        abs(noisePerlin2D(p * vec2(0.0005, 0.0017), uSeed ^ (351u))));
    float seamount = noiseLandmark2D01(p, 768.0, uSeed ^ (352u), NOISE_EUCLIDEAN, 0.22, 0.0, 0.50);
    float h = -235.0 + shelf - canyon * 145.0 + seamount * 275.0;
    return vec4(h, 486.0, -96.0, seamount);
}

#include "../../field_surface_accumulate.glsl"
