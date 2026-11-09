#version 450 core
// Biome 3: Taiga. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 3u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(p, vec2(0.8, -0.6));
    float valley = 1.0 - smoothstep(0.04, 0.26,
        abs(noisePerlin2D(q * vec2(0.0006, 0.0020), uSeed ^ (130u))));
    float esker = noiseRidge2D01(q * vec2(0.0014, 0.0050), uSeed ^ (131u));
    esker *= esker;
    esker *= esker;
    float shield = noiseLandmark2D01(p, 384.0, uSeed ^ (132u), NOISE_EUCLIDEAN, 0.22, 0.0, 0.68);
    float h = noisePerlin2D(p * 0.0012, uSeed ^ (133u)) * 75.0 - valley * 72.0;
    h += esker * 35.0 + shield * 250.0;
    return vec4(h, 342.0, -68.0, shield);
}

#include "../../field_surface_accumulate.glsl"
