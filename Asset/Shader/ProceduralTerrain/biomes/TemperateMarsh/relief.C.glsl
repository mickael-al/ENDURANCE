#version 450 core
// Biome 14: TemperateMarsh. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 14u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float channels = fieldNoise2(p * vec2(0.0025, 0.0045), 240u);
    float h = fieldSurfaceTerrace(fieldNoise2(p * 0.0011, 241u) * 18.0, 7.0, 0.86);
    h -= (1.0 - smoothstep(0.025, 0.13, abs(channels))) * 20.0;
    float island = fieldSurfaceLandmark(p, 384.0, 242u, 0.0, 0.22, 0.0, 0.60);
    h += island * 80.0;
    return vec4(h, 270.0, -38.0, island);
}

#include "../../field_surface_accumulate.glsl"
