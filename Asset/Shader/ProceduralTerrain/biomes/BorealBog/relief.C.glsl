#version 450 core
// Biome 4: BorealBog. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 4u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, 0.8));
    float basin = fieldSurfaceFbm(p * 0.0018, 140u) * 24.0 - 28.0;
    float rib = fieldRidge2(q * vec2(0.0008, 0.007), 143u);
    float island = fieldSurfaceLandmark(p, 384.0, 144u, 0.0, 0.21, 0.0, 0.58);
    float h = fieldSurfaceTerrace(basin, 10.0, 0.80) + rib * rib * 15.0 + island * 145.0;
    return vec4(h, 286.0, -42.0, island);
}

#include "../../field_surface_accumulate.glsl"
