#version 450 core
// Biome 21: RedDesert. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 21u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float dune = fieldSurfaceDune(p, vec2(0.6, 0.8), 360.0, 310u);
    float secondary = fieldNoise2(p * vec2(0.0017, 0.0041), 311u);
    float monument = fieldSurfaceLandmark(p, 768.0, 312u, 1.0, 0.22, 0.34, 0.52);
    float h = dune * 150.0 + secondary * 13.0;
    h += fieldSurfaceTerrace(monument * 440.0, 48.0, 0.40);
    return vec4(h, 436.0, -78.0, monument);
}

#include "../../field_surface_accumulate.glsl"
