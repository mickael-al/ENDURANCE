// Biome 12: TemperateForest. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_TemperateForest
#ifndef ENDURANCE_RELIEF_TEMPERATEFOREST_GLSL
#define ENDURANCE_RELIEF_TEMPERATEFOREST_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefTemperateForest(vec2 p)
{
    float hills = fieldSurfaceFbm(p * 0.0016, 220u) * 115.0;
    float tor = fieldSurfaceLandmark(p, 384.0, 223u, 0.0, 0.22, 0.0, 0.65);
    float h = hills + tor * (270.0 + 45.0 * fieldNoise2(p * 0.001, 224u));
    return vec4(h, 426.0, -80.0, tor);
}

#endif
#endif
