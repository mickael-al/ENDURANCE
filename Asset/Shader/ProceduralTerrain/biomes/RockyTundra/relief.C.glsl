#version 450 core
// Biome 5: RockyTundra. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 5u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, 0.6));
    float ridge = fieldRidge2(q * vec2(0.0011, 0.0022), 150u);
    float block = fieldSurfaceLandmark(p, 320.0, 151u, 2.0, 0.15, 0.42, 0.72);
    float h = fieldSurfaceTerrace((ridge * ridge - 0.45) * 170.0, 30.0, 0.68);
    h += block * (420.0 + 40.0 * fieldNoise2(p * 0.002, 152u));
    h += fieldNoise2(p * 0.006, 153u) * 10.0;
    return vec4(h, 406.0, -56.0, block);
}

#include "../../field_surface_accumulate.glsl"
