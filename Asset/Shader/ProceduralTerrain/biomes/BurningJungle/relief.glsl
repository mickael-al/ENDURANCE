// Biome 23: BurningJungle. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_BurningJungle
#ifndef ENDURANCE_RELIEF_BURNINGJUNGLE_GLSL
#define ENDURANCE_RELIEF_BURNINGJUNGLE_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefBurningJungle(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, -0.6));
    float flow = fieldSurfaceFbm(q * vec2(0.0008, 0.0028), 330u) * 76.0;
    float basalt = fieldSurfaceLandmark(p, 128.0, 333u, 2.0, 0.15, 0.40, 0.82);
    float h = fieldSurfaceTerrace(flow, 32.0, 0.62);
    h += basalt * (520.0 + 55.0 * fieldNoise2(p * 0.0015, 334u));
    return vec4(h, 526.0, -108.0, basalt);
}

#endif
#endif
