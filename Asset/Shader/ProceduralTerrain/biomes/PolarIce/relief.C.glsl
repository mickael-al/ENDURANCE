#version 450 core
// Biome 0: PolarIce. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 0u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // A cellular basin is a profile, not a cliff at every cell boundary.
    // Warped 240 m bowls sit inside kilometre-scale plain / plateau regions.
    vec2 q = landWarp(p, 1450.0, 100.0, 101u);
    float region = noiseSimplex2D(p / 4300.0, uSeed ^ 102u);
    float lake = smoothstep(0.05, 0.60, region);
    vec2 cell = noiseCellularDistancesSquared2D(q / 240.0, uSeed ^ 103u);
    float bowl = smoothstep(0.04, 0.94, sqrt(cell.x));
    float plateau = smoothstep(0.08, 0.62,
        noiseSimplex2D(q / 1700.0, uSeed ^ 104u));
    float h = landFbm(p / 3900.0, 105u) * 94.0;
    h += (bowl - 0.45) * mix(76.0, 14.0, lake);
    h += plateau * (1.0 - lake) * 170.0 - lake * 110.0;
    h += noiseSimplex2D(q / 42.0, uSeed ^ 106u) * 1.3 * (1.0 - 0.8 * lake);

    // The exceptional shelf shares the same compact footprint in 2D and 3D.
    LandSite site = landSite(p, 2400.0, 107u, 0.70);
    float approach = 1.0 - smoothstep(0.10, 0.90, length(site.q) / site.radius);
    h += site.mask * approach * mix(25.0, 72.0, site.variant);
    return vec4(h, landBand(site, 560.0), 0.0, site.mask);
}

#include "../../field_surface_accumulate.glsl"
