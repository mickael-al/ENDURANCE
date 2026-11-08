// Biome 25: Marine. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_Marine
#ifndef ENDURANCE_RELIEF_MARINE_GLSL
#define ENDURANCE_RELIEF_MARINE_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefMarine(vec2 p)
{
    float shelf = fieldSurfaceTerrace(fieldNoise2(p * 0.0007, 350u) * 85.0, 42.0, 0.80);
    float canyon = 1.0 - smoothstep(0.035, 0.20,
        abs(fieldNoise2(p * vec2(0.0005, 0.0017), 351u)));
    float seamount = fieldSurfaceLandmark(p, 768.0, 352u, 0.0, 0.22, 0.0, 0.50);
    float h = -235.0 + shelf - canyon * 145.0 + seamount * 275.0;
    return vec4(h, 486.0, -96.0, seamount);
}

#endif
#endif
