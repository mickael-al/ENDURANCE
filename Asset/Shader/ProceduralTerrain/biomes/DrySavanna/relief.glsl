// Biome 16: DrySavanna. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_DrySavanna
#ifndef ENDURANCE_RELIEF_DRYSAVANNA_GLSL
#define ENDURANCE_RELIEF_DRYSAVANNA_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefDrySavanna(vec2 p)
{
    float pediment = fieldSurfaceFbm(p * 0.0011, 260u) * 60.0;
    float inselberg = fieldSurfaceLandmark(p, 512.0, 263u, 0.0, 0.22, 0.0, 0.55);
    float h = mix(pediment, fieldSurfaceTerrace(pediment, 32.0, 0.80), 0.35);
    h += inselberg * (470.0 + 55.0 * fieldNoise2(p * 0.001, 264u));
    return vec4(h, 386.0, -72.0, inselberg);
}

#endif
#endif
