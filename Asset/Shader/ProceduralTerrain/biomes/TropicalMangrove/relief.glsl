// Biome 24: TropicalMangrove. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_TropicalMangrove
#ifndef ENDURANCE_RELIEF_TROPICALMANGROVE_GLSL
#define ENDURANCE_RELIEF_TROPICALMANGROVE_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefTropicalMangrove(vec2 p)
{
    float tide = fieldNoise2(p * 0.0011, 340u) * 20.0;
    float channel = 1.0 - smoothstep(0.025, 0.12,
        abs(fieldNoise2(p * vec2(0.0024, 0.0046), 341u)));
    float island = fieldSurfaceLandmark(p, 384.0, 342u, 0.0, 0.21, 0.0, 0.55);
    float h = fieldSurfaceTerrace(tide, 6.0, 0.88) - channel * 25.0 + island * 100.0;
    return vec4(h, 310.0, -52.0, island);
}

#endif
#endif
