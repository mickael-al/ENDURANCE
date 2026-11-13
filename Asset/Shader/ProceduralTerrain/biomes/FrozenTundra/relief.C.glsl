#version 450 core
// Biome 1: FrozenTundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// A broad, ice-rounded ridge with a curved crest and an excavated side cirque.
// Its summit remains at the anchor; increasing its footprint does not increase
// its height. Ribs are subordinate to the whole massif, never separate cones.
float frozenMassif(LandSite site)
{
    if (site.mask <= 0.0) return 0.0;
    vec2 v = site.q / site.radius;
    float radial = length(v);
    float crest = v.y - 0.14 * sin(v.x * 3.1) - 0.07 * v.x;
    float across = abs(crest) / mix(0.52, 0.62, site.variant);
    float along = abs(v.x) / 0.91;
    float crown = exp(-1.45 * pow(across, 1.70)
        - 1.40 * pow(along, 2.40));

    // A single cirque opens one flank rather than cutting a symmetric crater.
    vec2 cirque = (v - vec2(0.20, 0.43)) / vec2(0.38, 0.27);
    float hollow = exp(-dot(cirque, cirque) * 1.7)
        * smoothstep(0.06, 0.30, v.y);
    float ribs = landRidges(vec2(v.x * 3.8, crest * 3.0)
        + site.variant * vec2(13.0, 29.0), 212u);
    float shoulder = 1.0 - 0.14 * (1.0 - ribs)
        * smoothstep(0.08, 0.40, radial);
    float profile = crown * shoulder * (1.0 - 0.46 * hollow);
    return mix(420.0, 560.0, site.variant) / 0.35 * profile * site.mask;
}

vec4 fieldRelief(vec2 p)
{
    vec2 q = landWarp(p, 5950.0, 315.0, 201u);
    float broad = landFbm(q / 9450.0, 202u);
    // Flatten the plain gently; preserve broad convex rises rather than bowls.
    float h = broad * abs(broad) * 120.0;
    float dome = noiseSimplex2D(q / 2275.0, uSeed ^ 203u);
    dome = smoothstep(0.08, 0.70, dome);
    float region = smoothstep(-0.45, 0.45,
        noiseSimplex2D(p / 16800.0, uSeed ^ 204u));
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

    // Nominal spacing 2 -> 8 km; footprint 0.8--1.12 -> 2--2.8 km wide.
    // Rotated, warped placement and empty sites remove the regular rows.
    LandSite mountain = landSparseSite(p, 8000.0, 0.625, 211u, 0.80);
    h += frozenMassif(mountain);

    // Small underground chambers have their own, much denser population:
    // 2 RKM -> one candidate every 500 m, independently of the old pingos.
    LandSite cave = landRkmSite(p, 2.0, 209u);
    float support = max(site.mask, cave.mask);
    float band = max(landBand(site, 544.0), landBand(cave, 192.0));
    return vec4(h, band, 0.0, support);
}

#include "../../field_surface_accumulate.glsl"
