// Biome 15: RockDesert. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_RockDesert
#ifndef ENDURANCE_RELIEF_ROCKDESERT_GLSL
#define ENDURANCE_RELIEF_ROCKDESERT_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefRockDesert(vec2 p)
{
    float floorWaves = fieldSurfaceDune(p, vec2(0.8, 0.6), 420.0, 250u) * 62.0;
    float mesa = fieldSurfaceLandmark(p, 640.0, 251u, 0.0, 0.22, 0.58, 0.64);
    float layers = fieldSurfaceTerrace(mesa * 570.0, 54.0, 0.42);
    float h = floorWaves + layers + fieldNoise2(p * 0.002, 252u) * 8.0;
    return vec4(h, 506.0, -90.0, mesa);
}

#endif
#endif
