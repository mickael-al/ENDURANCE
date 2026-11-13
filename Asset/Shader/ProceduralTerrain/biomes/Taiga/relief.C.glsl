#version 450 core
// Biome 3: Taiga. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 3u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(landWarp(p, 2100.0, 230.0, 401u), vec2(0.8, -0.6));
    float meander = noiseSimplex2D(q / vec2(2100.0, 1400.0), uSeed ^ 402u);
    // Curved cordons have broad profiles and taper out into lateral passes.
    float axis = q.x / 510.0 + meander * 1.35;
    float rib = 0.5 + 0.5 * cos(axis * 3.14159265359);
    rib = rib * rib * (3.0 - 2.0 * rib);
    float passes = smoothstep(-0.32, 0.38,
        noiseSimplex2D(q / vec2(1500.0, 780.0), uSeed ^ 403u));
    float region = noiseSimplex2D(p / 5100.0, uSeed ^ 404u) * 0.5 + 0.5;
    float h = landFbm(q / vec2(3300.0, 5100.0), 405u) * 105.0;
    h += (rib - 0.30) * passes * mix(85.0, 190.0, region);
    h += noiseSimplex2D(q / vec2(85.0, 230.0), uSeed ^ 406u) * 2.0 * rib;

    LandSite site = landSite(p, 2800.0, 407u, 0.65);
    vec2 local = site.q / (site.radius * vec2(0.86, 0.46));
    float ramp = 1.0 - smoothstep(0.0, 1.0, dot(local, local));
    h += site.mask * ramp * mix(30.0, 80.0, site.variant);
    return vec4(h, landBand(site, 544.0), 0.0, site.mask);
}

#include "../../field_surface_accumulate.glsl"
