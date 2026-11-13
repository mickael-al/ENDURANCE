#version 450 core
// Biome 2: Tundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 2u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 warped = landWarp(p, 1900.0, 160.0, 301u);
    vec2 q = noiseRotate2D(warped, vec2(0.6, 0.8));
    // Long rolling tongues, soft shelves and broad saddles. The crossed low
    // frequency term prevents every valley from becoming a parallel rail.
    float broad = landFbm(q / vec2(1850.0, 4600.0), 302u);
    float lobes = landFbm(q / vec2(520.0, 1700.0), 303u);
    float region = smoothstep(-0.4, 0.55,
        noiseSimplex2D(p / 4300.0, uSeed ^ 304u));
    float h = broad * 112.0 + lobes * mix(20.0, 57.0, region);
    h = mix(h, landTerrace(h, 40.0, 0.48), 0.22 * region);
    h += noiseSimplex2D(q / 75.0, uSeed ^ 305u) * 1.4;

    LandSite site = landSite(p, 2600.0, 307u, 0.58);
    // An open half-cirque: the positive-X mouth stays at ordinary ground level.
    vec2 local = site.q / site.radius;
    float bowl = 1.0 - smoothstep(0.05, 0.82, length(local));
    float openSide = 1.0 - smoothstep(-0.30, 0.35, local.x);
    h -= site.mask * bowl * openSide * mix(25.0, 75.0, site.variant);
    return vec4(h, landBand(site, 512.0), 0.0, site.mask);
}

#include "../../field_surface_accumulate.glsl"
