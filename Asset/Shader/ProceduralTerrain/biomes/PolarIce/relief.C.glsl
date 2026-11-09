#version 450 core
// Biome 0: PolarIce. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#define FIELD_BIOME_ID 0u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_relief_common.glsl"

const float POLAR_BASIN_SPACING = 64.0;
const float POLAR_BASIN_WARP = 1.618;
const float POLAR_ROLL_AMPLITUDE = 18.0;
const float POLAR_FINE_AMPLITUDE = 2.5;

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(p, vec2(0.8, 0.6));
    float spacing = max(POLAR_BASIN_SPACING, 4.0);
    vec2 warp = vec2(noisePerlin2D(q / (spacing * 3.0), uSeed ^ (100u)),
        noisePerlin2D(q / (spacing * 3.0), uSeed ^ (101u)));
    vec2 sites = noiseCellularDistancesSquared2D(q / spacing + warp * POLAR_BASIN_WARP, uSeed ^ (102u)) * 10.0;

    float h = sites.x;
    h += noisePerlin2D(p * 0.0006, uSeed ^ (103u)) * POLAR_ROLL_AMPLITUDE;
    h += noisePerlin2D(p * 0.04, uSeed ^ (104u)) * POLAR_FINE_AMPLITUDE;
    h += clamp(noiseContrast01(noisePerlin2D(p * 0.002, uSeed ^ (102u)) - 0.05, 40.0), 0.0, 1.0) * 300;//falaise

    float lac = clamp(noiseContrast01(noisePerlin2D(p * 0.0002, uSeed ^ (102u)) - 0.05, 40.0), 0.0, 1.0);

    h -= lac * 250;//lac glacé
    h += noiseContrast01(noiseTriangle01(lac), 2.0) * noisePerlin2D01(p * 0.005, uSeed ^ (102u)) * 25;

    return vec4(h, clamp(noiseContrast01(noisePerlin2D01(p * 0.001, uSeed ^ (104u))-0.1, 2.0),0.0,1.0) * 241, -40.0, 0.0);
}

#include "../../field_surface_accumulate.glsl"
