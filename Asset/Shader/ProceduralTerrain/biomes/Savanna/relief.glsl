// Biome 17: Savanna. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_Savanna
#ifndef ENDURANCE_RELIEF_SAVANNA_GLSL
#define ENDURANCE_RELIEF_SAVANNA_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefSavanna(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, -0.8));
    float floorWaves = fieldSurfaceFbm(q * vec2(0.0009, 0.0016), 270u) * 85.0;
    float kopje = fieldSurfaceLandmark(p, 384.0, 273u, 0.0, 0.20, 0.0, 0.60);
    float h = floorWaves + kopje * (330.0 + 45.0 * fieldNoise2(p * 0.0015, 274u));
    return vec4(h, 390.0, -74.0, kopje);
}

#endif
#endif
