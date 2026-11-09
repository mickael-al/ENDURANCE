#version 450 core
// Biome 1: FrozenTundra. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    /*float polygon = noiseCellularDistance2D(p / 96.0, uSeed ^ (110u), NOISE_CHEBYSHEV);
    float raisedRim = smoothstep(0.24, 0.34, polygon) *
                      (1.0 - smoothstep(0.40, 0.48, polygon));
    float pingo = noiseLandmark2D01(p, 256.0, uSeed ^ (111u), NOISE_EUCLIDEAN, 0.21, 0.0, 0.70);
    float h = noiseFbmTerrain2D(p * 0.0015, uSeed, 112u) * 38.0;
    h += raisedRim * 8.0 - smoothstep(0.1, 0.32, polygon) * 12.0 + pingo * 125.0;
    return vec4(h, 270.0, -36.0, pingo);*/
    float ridge = noiseRidge2D01(p * 0.002, uSeed ^ (150u));
    float h = ridge * ridge * 100.0;

    float montagne = max(noiseContrast01(noisePerlin2D01(p * 0.001, uSeed ^ (104u)) - 0.2, 4.0), 0.0);

    h += montagne * 1000;
    h += montagne * noiseRidge2D01(p * 0.02, uSeed ^ (150u)) * 200;
    h += montagne * noiseRidge2D01(p * 0.08, uSeed ^ (150u)) * 50;
    h += noisePerlin2D01(p * 0.01, uSeed ^ (104u)) * 10;
    return vec4(h, montagne*500.0, 0.0, 0.0);
}

#include "../../field_surface_accumulate.glsl"
