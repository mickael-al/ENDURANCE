#version 450 core
// Biome 3: Taiga. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 3u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// A long folded ridge rather than a pointed mound: its wooded foothills run
// out into offset spurs, while unequal slopes and lateral valleys open passes.
float taigaRange(LandSite site)
{
    if (site.mask <= 0.0) return 0.0;
    vec2 u = site.q / site.radius;
    float radial = length(u);
    float phase = site.variant * 6.28318530718;
    float bend = 0.13 * (sin(u.x * 4.6 + phase) - sin(phase));
    bend += 0.045 * noiseSimplex2D(u * 3.7 + site.variant * 21.0,
        uSeed ^ 423u) * smoothstep(0.0, 0.18, radial);
    float transverse = u.y - bend;
    float flankWidth = mix(0.42, 0.70, smoothstep(-0.16, 0.16, transverse));
    float across = pow(max(1.0 - abs(transverse) / flankWidth, 0.0), 0.82);
    float along = pow(max(1.0 - abs(u.x) / 0.98, 0.0), 0.60);
    float spine = across * along;
    vec2 spurA = u - vec2(-0.28, 0.30);
    spurA.y -= spurA.x * 0.42;
    vec2 spurB = u - vec2(0.33, -0.24);
    spurB.y += spurB.x * 0.60;
    float foothillA = 0.60 * pow(max(1.0 - length(
        spurA / vec2(0.55, 0.31)), 0.0), 0.75);
    float foothillB = 0.48 * pow(max(1.0 - length(
        spurB / vec2(0.47, 0.27)), 0.0), 0.75);
    float profile = max(spine, max(foothillA, foothillB));
    // Branching roughness strengthens away from the crest. The central
    // watershed stays intact; the surrounding relief contains shallow passes.
    float drainage = landRidges(u * vec2(5.5, 7.2) + site.variant * 37.0, 426u);
    float flank = smoothstep(0.04, 0.32, abs(transverse));
    profile *= mix(1.0, 0.72 + 0.28 * drainage,
        flank * smoothstep(0.0, 0.20, radial));
    return (700.0 / 0.35) * profile * site.mask;
}

vec4 fieldRelief(vec2 p)
{
    vec2 q = noiseRotate2D(landWarp(p, 7350.0, 805.0, 401u), vec2(0.8, -0.6));
    // Broad irregular foothills replace the repeated cosine cordons. These
    // wavelengths are 3-4 times the former base hills, at the same amplitude.
    float rib = landRidges(q / vec2(4800.0, 8200.0), 403u);
    float region = noiseSimplex2D(p / 17850.0, uSeed ^ 404u) * 0.5 + 0.5;
    float h = landFbm(q / vec2(11550.0, 17850.0), 405u) * 105.0;
    h += (rib - 0.30) * mix(85.0, 190.0, region);
    h += noiseSimplex2D(q / vec2(85.0, 230.0), uSeed ^ 406u) * 2.0 * rib;

    LandSite site = landSite(p, 2800.0, 407u, 0.65);
    vec2 local = site.q / (site.radius * vec2(0.86, 0.46));
    float ramp = 1.0 - smoothstep(0.0, 1.0, dot(local, local));
    h += site.mask * ramp * mix(30.0, 80.0, site.variant);

    // 8-km nominal placement, 2.4-3.36-km footprint: spacing x4 and width x3.
    LandSite mountain = landSparseSite(p, 8000.0, 0.75, 421u, 0.80);
    h += taigaRange(mountain);

    if (noiseSimplexFbm2D01(p / 3000.0, 420u, 6, 2.0, 0.5) > 0.6)
    {
        h -= 100;
    }
    return vec4(h, landBand(site, 544.0), 0.0, site.mask);
}

#include "../../field_surface_accumulate.glsl"
