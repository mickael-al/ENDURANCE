#version 450 core
// Biome 2: Tundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 2u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"
#include "cave_sites.glsl"

// A wide, asymmetric rock massif: one curved arête, offset shoulders and
// two glacial scoops in its flanks. Its summit is anchored at q=0 so that
// widening the footprint does not silently lower the requested 800-m rise.
float tundraMassif(LandSite site)
{
    if (site.mask <= 0.0) return 0.0;
    vec2 u = site.q / site.radius;
    float radial = length(u);
    float phase = site.variant * 6.28318530718;
    vec2 distortion = vec2(
        noiseSimplex2D(u * 2.8 + vec2(17.0, 31.0), uSeed ^ 323u),
        noiseSimplex2D(u * 2.8 + vec2(53.0, 11.0), uSeed ^ 325u));
    u += distortion * (0.075 * smoothstep(0.0, 0.20, radial));
    float crest = 0.13 * (sin(u.x * 4.3 + phase) - sin(phase));
    float coreDistance = length(vec2(u.x * 0.95, (u.y - crest) * 1.45));
    float core = pow(max(1.0 - coreDistance, 0.0), 0.78);
    float shoulderA = 0.66 * pow(max(1.0 - length(
        (u - vec2(-0.30, 0.24)) / vec2(0.52, 0.40)), 0.0), 0.72);
    float shoulderB = 0.48 * pow(max(1.0 - length(
        (u - vec2(0.35, -0.20)) / vec2(0.42, 0.44)), 0.0), 0.85);
    float profile = max(core, max(shoulderA, shoulderB));
    float cirqueA = 1.0 - smoothstep(0.12, 1.0,
        length((u - vec2(0.18, 0.46)) / vec2(0.29, 0.30)));
    float cirqueB = 1.0 - smoothstep(0.08, 1.0,
        length((u - vec2(-0.37, -0.34)) / vec2(0.27, 0.23)));
    profile = max(profile - 0.19 * max(cirqueA, cirqueB), 0.0);
    // Feedback ridges cut subordinate buttresses into the wide mass, never
    // replacing its silhouette with isolated noise peaks.
    float ridges = landRidges(u * vec2(6.2, 4.6) + site.variant * 19.0, 326u);
    profile *= mix(1.0, 0.78 + 0.22 * ridges, smoothstep(0.0, 0.22, radial));
    return (800.0 / 0.35) * profile * site.mask;
}

vec4 fieldRelief(vec2 p)
{
    vec2 warped = landWarp(p, 5700.0, 480.0, 301u);
    vec2 q = noiseRotate2D(warped, vec2(0.6, 0.8));
    // Long rolling tongues, soft shelves and broad saddles. The crossed low
    // frequency term prevents every valley from becoming a parallel rail.
    float broad = landFbm(q / vec2(5550.0, 13800.0), 302u);
    float lobes = landFbm(q / vec2(2080.0, 6800.0), 303u);
    float region = smoothstep(-0.4, 0.55,
        noiseSimplex2D(p / 12900.0, uSeed ^ 304u));
    float h = broad * 112.0 + lobes * mix(20.0, 57.0, region);
    h = mix(h, landTerrace(h, 40.0, 0.48), 0.22 * region);
    h += noiseSimplex2D(q / 75.0, uSeed ^ 305u) * 1.4;

    LandSite site = landSite(p, 2600.0, 307u, 0.58);
    // An open half-cirque: the positive-X mouth stays at ordinary ground level.
    vec2 local = site.q / site.radius;
    float bowl = 1.0 - smoothstep(0.05, 0.82, length(local));
    float openSide = 1.0 - smoothstep(-0.30, 0.35, local.x);
    h -= site.mask * bowl * openSide * mix(25.0, 75.0, site.variant);

    // Nominal spacing grows from 1 to 6 km, footprint from .4-.56 to 2-2.8 km.
    // Rotated, smoothly deformed placement and empty cells break aligned rows.
    LandSite mountain = landSparseSite(p, 6000.0, 0.833333333, 321u, 0.80);
    h += tundraMassif(mountain);

    // Independent 20-m boulders, nominally one site per 2 km. Only their
    // compact footprint opens a volume band; the whole cell is not voxelized.
    LandSite rocks = landRkmSite(p, 0.5, 322u);
    rocks.mask *= 1.0 - smoothstep(27.0, 42.0, length(rocks.q));
    float band = max(landBand(site, 512.0), landBand(rocks, 192.0));
    // A separate cave population can open the ground where neither old 3D
    // sculpture exists. Its support is disjoint from both original bands.
    if (max(site.mask, rocks.mask) <= 0.0)
        band = max(band, tundraCaveBand(tundraCaveSite(p)));
    return vec4(h, band,
        0.0, max(site.mask, rocks.mask));
}

#include "../../field_surface_accumulate.glsl"
