#version 450 core
// Biome 18: DryJungle. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 18u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float floorWaves = fieldSurfaceFbm(p * 0.0013, 280u) * 55.0;
    float tower = fieldSurfaceLandmark(p, 128.0, 283u, 1.0, 0.22, 0.18, 0.88);
    float h = fieldSurfaceTerrace(floorWaves, 22.0, 0.78);
    h += tower * (480.0 + 75.0 * fieldNoise2(p * 0.002, 284u));
    return vec4(h, 506.0, -100.0, tower);
}

#include "../../field_surface_accumulate.glsl"
