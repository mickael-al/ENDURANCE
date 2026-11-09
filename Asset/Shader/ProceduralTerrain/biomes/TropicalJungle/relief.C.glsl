#version 450 core
// Biome 19: TropicalJungle. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 19u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float floorWaves = fieldSurfaceFbm(p * 0.0010, 290u) * 65.0;
    float tower = fieldSurfaceLandmark(p, 128.0, 293u, 0.0, 0.22, 0.36, 0.90);
    float h = floorWaves + tower * (560.0 + 65.0 * fieldNoise2(p * 0.0017, 294u));
    return vec4(h, 526.0, -112.0, tower);
}

#include "../../field_surface_accumulate.glsl"
