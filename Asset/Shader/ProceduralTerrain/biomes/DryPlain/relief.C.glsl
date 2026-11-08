#version 450 core
// Biome 10: DryPlain. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 10u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float h = fieldNoise2(p * 0.0009, 200u) * 44.0 + fieldNoise2(p * 0.003, 201u) * 10.0;
    float butte = fieldSurfaceLandmark(p, 512.0, 202u, 0.0, 0.20, 0.45, 0.52);
    h += butte * (360.0 + fieldNoise2(p * 0.0011, 203u) * 65.0);
    return vec4(h, 342.0, -60.0, butte);
}

#include "../../field_surface_accumulate.glsl"
