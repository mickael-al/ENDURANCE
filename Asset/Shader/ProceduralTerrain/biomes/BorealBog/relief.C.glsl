#version 450 core
// Biome 4: BorealBog. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 4u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = landWarp(p, 1750.0, 170.0, 501u);
    vec2 cell = noiseCellularDistancesSquared2D(q / 680.0, uSeed ^ 502u);
    // Low clustered basins, not a carpet of pits. Smooth shoulders connect
    // their wide islands; the complete surface remains ordinary solid terrain.
    float basin = 1.0 - smoothstep(0.04, 0.53, cell.x);
    float region = smoothstep(-0.35, 0.50,
        noiseSimplex2D(p / 4100.0, uSeed ^ 503u));
    float h = landFbm(p / 3400.0, 504u) * 45.0;
    h -= basin * mix(13.0, 37.0, region);
    vec2 ribs = noiseRotate2D(q, vec2(0.6, -0.8));
    float cordon = noiseSimplex2D(ribs / vec2(220.0, 1100.0), uSeed ^ 505u);
    h += smoothstep(-0.10, 0.65, cordon) * 22.0 * (1.0 - 0.5 * basin);
    h = mix(h, landTerrace(h, 18.0, 0.49), 0.20);
    h += noiseSimplex2D(q / 90.0, uSeed ^ 506u) * 0.7;

    LandSite site = landSite(p, 2500.0, 507u, 0.55);
    vec2 local = site.q / (site.radius * vec2(0.85, 0.55));
    float island = 1.0 - smoothstep(0.0, 1.0, dot(local, local));
    h += site.mask * island * mix(20.0, 54.0, site.variant);
    return vec4(h, landBand(site, 416.0), 0.0, site.mask);
}

#include "../../field_surface_accumulate.glsl"
