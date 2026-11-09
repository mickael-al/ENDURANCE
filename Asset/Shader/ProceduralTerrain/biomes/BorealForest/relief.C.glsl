#version 450 core
// Biome 7: BorealForest. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 7u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, -0.6));
    float drumlin = fieldNoise2(q * vec2(0.0010, 0.0041), 170u);
    float shield = fieldSurfaceLandmark(p, 384.0, 171u, 0.0, 0.22, 0.0, 0.65);
    float h = fieldSurfaceFbm(p * 0.0013, 172u) * 65.0 + drumlin * 32.0;
    h += shield * 270.0;
    return vec4(h, 376.0, -76.0, shield);
}

#include "../../field_surface_accumulate.glsl"
