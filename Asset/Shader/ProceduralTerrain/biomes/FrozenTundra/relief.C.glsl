#version 450 core
// Biome 1: FrozenTundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = landWarp(p, 1700.0, 90.0, 201u);
    float broad = landFbm(q / 2700.0, 202u);
    // Flatten the plain gently; preserve broad convex rises rather than bowls.
    float h = broad * abs(broad) * 120.0;
    float dome = noiseSimplex2D(q / 650.0, uSeed ^ 203u);
    dome = smoothstep(0.08, 0.70, dome);
    float region = smoothstep(-0.45, 0.45,
        noiseSimplex2D(p / 4800.0, uSeed ^ 204u));
    h += dome * mix(24.0, 100.0, region);
    // Shallow polygon seams: under 0.7 m deep at the default detail scale.
    vec2 cell = sqrt(noiseCellularDistancesSquared2D(q / 145.0, uSeed ^ 205u));
    float seam = 1.0 - smoothstep(0.012, 0.15, cell.y - cell.x);
    h -= seam * 1.8;
    h += noiseSimplex2D(q / 52.0, uSeed ^ 206u) * 1.0;

    LandSite site = landSite(p, 2200.0, 207u, 0.62);
    vec2 local = site.q / (site.radius * vec2(0.76, 0.68));
    float mound = 1.0 - smoothstep(0.0, 1.0, dot(local, local));
    h += site.mask * mound * mix(85.0, 150.0, site.variant);

    // Ridged multifractal buttresses, low-frequency fBm and a bounded mountain
    // profile produce actual 420--560 m peaks at the reference detailScale=.35.
    // Mountain rate is a tuning default (.5 RKM); the user specified only
    // the cave rate for this biome.
    LandSite mountain = landRkmSite(p, 0.50, 211u);
    h += landMountain(mountain, mix(420.0, 560.0, mountain.variant), 212u);

    // Small underground chambers have their own, much denser population:
    // 2 RKM -> one candidate every 500 m, independently of the old pingos.
    LandSite cave = landRkmSite(p, 2.0, 209u);
    float support = max(site.mask, cave.mask);
    float band = max(landBand(site, 544.0), landBand(cave, 192.0));
    return vec4(h, band, 0.0, support);
}

#include "../../field_surface_accumulate.glsl"
