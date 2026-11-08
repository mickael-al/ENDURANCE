#version 450 core
// Biome 11: Meadow. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 11u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, 0.6));
    float waves = fieldSurfaceFbm(q * vec2(0.0007, 0.0015), 210u) * 96.0;
    float knoll = fieldSurfaceLandmark(p, 512.0, 213u, 0.0, 0.22, 0.0, 0.55);
    float h = waves + knoll * 125.0;
    h += fieldNoise2(q * vec2(0.0012, 0.004), 214u) * 10.0;
    return vec4(h, 302.0, -52.0, knoll);
}

#include "../../field_surface_accumulate.glsl"
