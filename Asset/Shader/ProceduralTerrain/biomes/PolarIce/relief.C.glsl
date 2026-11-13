#version 450 core
// Biome 0: PolarIce. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 0u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"
#include "shape.glsl"

vec4 fieldRelief(vec2 p)
{
    // A cellular basin is a profile, not a cliff at every cell boundary.
    // Widen the ground undulations without raising their amplitude. The broad
    // bowls and plateaux leave quiet expanses between the exceptional massifs.
    vec2 q = landWarp(p, 5800.0, 400.0, 101u);
    float region = noiseSimplex2D(p / 12900.0, uSeed ^ 102u);
    float lake = smoothstep(0.05, 0.60, region);
    vec2 cell = noiseCellularDistancesSquared2D(q / 700.0, uSeed ^ 103u);
    float bowl = smoothstep(0.04, 0.94, sqrt(cell.x));
    float plateau = smoothstep(0.08, 0.62,
        noiseSimplex2D(q / 6800.0, uSeed ^ 104u));
    float h = landFbm(p / 11700.0, 105u) * 94.0;
    h += (bowl - 0.45) * mix(76.0, 14.0, lake);
    h += plateau * (1.0 - lake) * 170.0 - lake * 110.0;
    h += noiseSimplex2D(q / 42.0, uSeed ^ 106u) * 1.3 * (1.0 - 0.8 * lake);

    // The exceptional shelf shares the same compact footprint in 2D and 3D.
    LandSite site = landSparseSite(p, 9600.0, 0.25, 107u, 0.65);
    float approach = 1.0 - smoothstep(0.10, 0.90, length(site.q) / site.radius);
    h += site.mask * approach * mix(25.0, 72.0, site.variant);

    // Five times the old nominal separation, with a fourfold footprint. The
    // curved glacial spine keeps the 450--580 m height, over much wider flanks.
    // Empty candidates and warped placement break the former visible grid.
    LandSite massif = landSparseSite(p, 20000.0, 0.80, 111u, 0.80);
    if (massif.mask > 0.0)
    {
        vec2 v = massif.q / massif.radius;
        float spine = v.y + 0.18 * v.x + 0.12 * v.x * v.x;
        float blade = length(vec2(v.x * 1.12, spine * 1.80));
        float peak = pow(max(1.0 - blade, 0.0), 0.95);
        float striations = landRidges(massif.q / vec2(1440.0, 760.0), 112u);
        float incision = mix(1.0, 0.76 + 0.24 * striations,
            smoothstep(0.0, 0.20, blade));
        float iceHeight = peak * incision * mix(450.0, 1580.0, massif.variant);
        iceHeight = mix(iceHeight, landTerrace(iceHeight, 38.0, 0.28), 0.20);
        h += massif.mask * iceHeight / 0.35;
    }

    // Four times the old separation, retaining narrow openings. Curved,
    // noise-shaped shoulders and tapered branches replace rectangular ends.
    // The deep incision is in the heightfield and cannot be band-clipped.
    LandSite rift = landSparseSite(p, 8000.0, 1.5, 209u, 0.80);
    float tear = polarRiftMask(rift);
    h -= tear * mix(700.0, 900.0, rift.variant) / 0.35;

    // Local caves only need the usual ~202 m vertical envelope. The deep
    // rift itself belongs to the heightfield, so its floor is never band-clipped.
    float support = max(site.mask, rift.mask);
    float band = max(landBand(site, 560.0), landBand(rift, 576.0));
    return vec4(h, band, 0.0, support);
}

#include "../../field_surface_accumulate.glsl"
