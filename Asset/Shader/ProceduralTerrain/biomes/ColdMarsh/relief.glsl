// Biome 9: ColdMarsh. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_ColdMarsh
#ifndef ENDURANCE_RELIEF_COLDMARSH_GLSL
#define ENDURANCE_RELIEF_COLDMARSH_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefColdMarsh(vec2 p)
{
    float lake = fieldCellular2(p / 256.0, 190u, 0.0);
    float basin = 1.0 - smoothstep(0.12, 0.31, lake);
    float pingo = fieldSurfaceLandmark(p, 512.0, 191u, 0.0, 0.18, 0.0, 0.55);
    float h = fieldSurfaceFbm(p * 0.0014, 192u) * 18.0 - basin * 42.0;
    h += pingo * 110.0;
    return vec4(h, 278.0, -40.0, pingo);
}

#endif
#endif
