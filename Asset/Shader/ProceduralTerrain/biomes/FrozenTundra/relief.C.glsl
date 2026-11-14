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
    float h = 0.0;

    return vec4(h, 0.0, 0.0, 0.0);
}

#include "../../field_surface_accumulate.glsl"
