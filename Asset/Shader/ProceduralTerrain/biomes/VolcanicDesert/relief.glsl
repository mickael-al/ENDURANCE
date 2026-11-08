// Biome 20: VolcanicDesert. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_VolcanicDesert
#ifndef ENDURANCE_RELIEF_VOLCANICDESERT_GLSL
#define ENDURANCE_RELIEF_VOLCANICDESERT_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefVolcanicDesert(vec2 p)
{
    float d = fieldCellular2(p / 768.0, 300u, 0.0);
    float shield = fieldSurfaceMound(d, 0.43);
    float caldera = 1.0 - smoothstep(0.055, 0.12, d);
    float flow = fieldNoise2(p * vec2(0.0015, 0.005), 301u) * 30.0;
    float basalt = fieldSurfaceLandmark(p, 512.0, 302u, 2.0, 0.14, 0.55, 0.60);
    float h = fieldSurfaceTerrace(shield * 460.0 - caldera * 240.0 + flow, 28.0, 0.70);
    h += basalt * 110.0;
    return vec4(h, 526.0, -120.0, max(shield, basalt));
}

#endif
#endif
