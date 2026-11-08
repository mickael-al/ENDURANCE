// Biome 0: PolarIce. The caller clamps and accumulates this 2D result.
// x: relative height; y: total 3D band thickness; z: band offset; w: rock mask.
// XYZ are in metres before detailScale; w is in [0,1].
#ifdef FIELD_ENABLE_RELIEF_PolarIce
#ifndef ENDURANCE_RELIEF_POLARICE_GLSL
#define ENDURANCE_RELIEF_POLARICE_GLSL

#include "../../biome_relief_common.glsl"

const float POLAR_BASIN_SPACING = 64.0;
const float POLAR_BASIN_WARP = 1.618;
const float POLAR_ROLL_AMPLITUDE = 18.0;
const float POLAR_FINE_AMPLITUDE = 2.5;

vec4 fieldReliefPolarIce(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, 0.6));
    float spacing = max(POLAR_BASIN_SPACING, 4.0);
    vec2 warp = vec2(fieldNoise2(q / (spacing * 3.0), 100u),
        fieldNoise2(q / (spacing * 3.0), 101u));
    vec2 sites = fieldCellular2Distances(q / spacing + warp * POLAR_BASIN_WARP, 102u) * 10.0;

    float h = sites.x;
    h += fieldNoise2(p * 0.0006, 103u) * POLAR_ROLL_AMPLITUDE;
    h += fieldNoise2(p * 0.04, 104u) * POLAR_FINE_AMPLITUDE;
    h += clamp(contrast(fieldNoise2(p * 0.002, 102u) - 0.05, 40.0), 0.0, 1.0) * 300;//falaise

    float lac = clamp(contrast(fieldNoise2(p * 0.0002, 102u) - 0.05, 40.0), 0.0, 1.0);

    h -= lac * 250;//lac glacé
    h += contrast(courbe01(lac), 2.0) * fieldUnsignedNoise(p * 0.005, 102u) * 25;

    return vec4(h, clamp(contrast(fieldUnsignedNoise(p * 0.001, 104u)-0.1,2.0),0.0,1.0) * 241, -40.0, 0.0);
}

#endif
#endif
