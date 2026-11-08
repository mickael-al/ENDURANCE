#version 450 core
// Biome 2: Tundra. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 2u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, -0.8));
    float h = fieldSurfaceFbm(q * vec2(0.0011, 0.0025), 120u) * 95.0;
    float tor = fieldSurfaceLandmark(p, 384.0, 123u, 0.0, 0.20, 0.0, 0.60);
    h += tor * 150.0 + fieldNoise2(q * vec2(0.002, 0.006), 124u) * 10.0;
    return vec4(h, 278.0, -44.0, tor);
}

#include "../../field_surface_accumulate.glsl"
