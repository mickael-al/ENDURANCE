// Biome 1: FrozenTundra. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_FrozenTundra
#ifndef ENDURANCE_RELIEF_FROZENTUNDRA_GLSL
#define ENDURANCE_RELIEF_FROZENTUNDRA_GLSL

#include "../../biome_relief_common.glsl"

vec4 fieldReliefFrozenTundra(vec2 p)
{
    /*float polygon = fieldCellular2(p / 96.0, 110u, 2.0);
    float raisedRim = smoothstep(0.24, 0.34, polygon) *
                      (1.0 - smoothstep(0.40, 0.48, polygon));
    float pingo = fieldSurfaceLandmark(p, 256.0, 111u, 0.0, 0.21, 0.0, 0.70);
    float h = fieldSurfaceFbm(p * 0.0015, 112u) * 38.0;
    h += raisedRim * 8.0 - smoothstep(0.1, 0.32, polygon) * 12.0 + pingo * 125.0;
    return vec4(h, 270.0, -36.0, pingo);*/
    float ridge = fieldRidge2(p * 0.002, 150u);
    float h = ridge * ridge * 100.0;

    float montagne = max(contrast(fieldUnsignedNoise(p * 0.001, 104u) - 0.2, 4.0), 0.0);

    h += montagne * 1000;
    h += montagne * fieldRidge2(p * 0.02, 150u) * 200;
    h += montagne * fieldRidge2(p * 0.08, 150u) * 50;
    h += fieldUnsignedNoise(p * 0.01, 104u) * 10;
    return vec4(h, montagne*500.0, 0.0, 0.0);
}

#endif
#endif
