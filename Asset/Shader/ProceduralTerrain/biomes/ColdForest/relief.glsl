// Biome 8: ColdForest. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_ColdForest
#ifndef ENDURANCE_RELIEF_COLDFOREST_GLSL
#define ENDURANCE_RELIEF_COLDFOREST_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefColdForest(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, 0.8));
    float valley = 1.0 - smoothstep(0.05, 0.28,
        abs(fieldNoise2(q * vec2(0.00055, 0.0017), 180u)));
    float buttress = fieldSurfaceLandmark(p, 384.0, 181u, 1.0, 0.22, 0.12, 0.68);
    float h = (1.0 - valley) * 145.0 - valley * 120.0;
    h += fieldNoise2(p * 0.0025, 182u) * 35.0 + buttress * 360.0;
    return vec4(h, 446.0, -96.0, buttress);
}

#endif
#endif
