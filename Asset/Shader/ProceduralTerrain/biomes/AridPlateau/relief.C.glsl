#version 450 core
// Biome 22: AridPlateau. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 22u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

vec4 fieldRelief(vec2 p)
{
    float mesa = noiseLandmark2D01(p, 512.0, uSeed ^ (320u), NOISE_CHEBYSHEV, 0.15, 0.62, 0.68);
    float floorWaves = noiseFbmTerrain2D(p * 0.0010, uSeed, 321u) * 45.0;
    float h = floorWaves + noiseTerrace(mesa * 540.0, 60.0, 0.32);
    return vec4(h, 486.0, -90.0, mesa);
}

#include "../../field_surface_accumulate.glsl"
