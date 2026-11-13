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

    // RKM is a nominal frequency per kilometre: .25 means a 4 km placement
    // cell. The tall ice is a wind-cut, asymmetric blade rather than a dome.
    // New macro amplitudes below are world metres at detailScale=.35.
    LandSite massif = landRkmSite(p, 0.25, 111u);
    if (massif.mask > 0.0)
    {
        vec2 v = massif.q / massif.radius;
        float blade = max(abs(v.x) * 1.12, abs(v.y + 0.18 * v.x) * 1.80);
        float peak = pow(max(1.0 - blade, 0.0), 1.15);
        float striations = landRidges(p / vec2(360.0, 190.0), 112u);
        float incision = mix(1.0, 0.76 + 0.24 * striations,
            smoothstep(0.0, 0.20, blade));
        float iceHeight = peak * incision * mix(450.0, 580.0, massif.variant);
        iceHeight = mix(iceHeight, landTerrace(iceHeight, 38.0, 0.28), 0.20);
        h += massif.mask * iceHeight / 0.35;
    }

    // Long tears with 160--210 m openings and near-vertical 16 m shoulders.
    // Carving after the massif also lets the rarer tears split an ice blade.
    LandSite rift = landRkmSite(p, 0.50, 109u);
    float bend = 18.0 * sin(rift.q.x / 120.0 + rift.variant * 6.2831853)
        + 8.0 * sin(rift.q.x / 57.0 + rift.variant * 2.0);
    float halfWidth = mix(80.0, 105.0, rift.variant);
    float crossSection = 1.0 - smoothstep(halfWidth - 8.0, halfWidth + 8.0,
        abs(rift.q.y - bend));
    float ends = 1.0 - smoothstep(0.62, 0.94, abs(rift.q.x) / rift.radius);
    float tear = rift.mask * ends * crossSection;
    h -= tear * mix(230.0, 330.0, rift.variant) / 0.35;

    // Local caves only need the usual ~202 m vertical envelope. The deep
    // rift itself belongs to the heightfield, so its floor is never band-clipped.
    float support = max(site.mask, rift.mask);
    float band = max(landBand(site, 560.0), landBand(rift, 576.0));
    return vec4(h, band, 0.0, support);
}

#include "../../field_surface_accumulate.glsl"
