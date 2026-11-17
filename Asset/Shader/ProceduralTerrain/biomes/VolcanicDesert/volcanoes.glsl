#ifndef ENDURANCE_VOLCANIC_DESERT_VOLCANOES_GLSL
#define ENDURANCE_VOLCANIC_DESERT_VOLCANOES_GLSL
#include "../../biome_relief_common.glsl"
#include "settings.glsl"

struct VolcanicSite
{
    vec2 center;
    float radius;
    float height;
    uint key;
    vec2 rotation;
    float aspect;
    float crater;
    float floorHeight;
};

float volcanicRandom(uint key, uint salt)
{
    return noiseInternalUintTo01(noiseInternalHash(key ^ salt));
}

VolcanicSite volcanicSiteInCell(ivec2 cell)
{
    uint key = noiseInternalHash2D(cell, uSeed ^ 0x20caa71du);
    vec2 jitter = vec2(volcanicRandom(key, 0x15bc371du), volcanicRandom(key, 0x837acf19u));
    float angle = 6.28318530718 * volcanicRandom(key, 0x93ac871bu);
    return VolcanicSite((vec2(cell) + 0.25 + 0.5 * jitter) * VOLCANIC_CELL_SIZE,
        mix(VOLCANIC_RADIUS_RANGE.x, VOLCANIC_RADIUS_RANGE.y, volcanicRandom(key, 0x1357acbdu)),
        mix(VOLCANIC_HEIGHT_RANGE.x, VOLCANIC_HEIGHT_RANGE.y, volcanicRandom(key, 0x93a57f11u)),
        key, vec2(cos(angle), sin(angle)), mix(0.82, 1.14, volcanicRandom(key, 0x4937abcdu)),
        mix(VOLCANIC_CRATER_RADIUS_RANGE.x, VOLCANIC_CRATER_RADIUS_RANGE.y, volcanicRandom(key, 0x817bce15u)),
        mix(VOLCANIC_CRATER_FLOOR_RANGE.x, VOLCANIC_CRATER_FLOOR_RANGE.y, volcanicRandom(key, 0x73aba135u)));
}

float volcanicConeHeight(vec2 p, VolcanicSite site)
{
    vec2 d = (p - site.center) / site.radius;
    // The bound includes the ellipse, radial distortion and domain warp.
    // All expensive noise evaluations are skipped outside the footprint.
    if (dot(d, d) >= 1.50 * 1.50) return 0.0;
    vec2 q = vec2(dot(d, site.rotation), dot(d, vec2(-site.rotation.y, site.rotation.x)));
    q.y /= site.aspect;
    vec2 phase = vec2(volcanicRandom(site.key, 0x187e5aa3u), volcanicRandom(site.key, 0x71bef723u)) * 31.0;
    // Warp vanishes at the centre: the bowl remains centred on its own vent.
    float warpWeight = smoothstep(0.0, 0.5, length(q));
    q += 0.055 * warpWeight * vec2(noisePerlin2D(q * 2.3 + phase, site.key ^ 0x5917u),
        noisePerlin2D(q * 2.3 + phase.yx, site.key ^ 0x2639u));
    float distance = length(q);
    vec2 direction = q / max(distance, 0.0001);
    float outline = 1.0 + 0.12 * noiseSimplex2D(direction * 2.4 + phase, site.key ^ 0x931bu);
    // Angular variation also fades at the centre, avoiding a polar singularity.
    float r = distance / mix(1.0, outline, smoothstep(0.0, site.crater, distance));
    if (r >= 1.0) return 0.0;

    float exterior = pow(clamp((1.0 - r) / (1.0 - site.crater), 0.0, 1.0), 1.55);
    float bowl = mix(site.floorHeight, 1.0, smoothstep(site.crater * 0.18, site.crater, r));
    float rimVariation = 1.0 + 0.055 * noiseSimplex2D(direction * 4.2 + phase,
        site.key ^ 0x2731u) * smoothstep(0.0, site.crater, r);
    // Radial gullies divide broad, asymmetric buttresses, rather than forming
    // concentric terraces. They fade before both the crater rim and the foot.
    float gullyNoise = abs(noiseSimplex2D(direction * 6.5 + q * 0.8 + phase, site.key ^ 0x17a3u));
    float gullies = 1.0 - smoothstep(0.03, 0.30, gullyNoise);
    float flank = smoothstep(site.crater, site.crater + 0.16, r) * (1.0 - smoothstep(0.78, 1.0, r));
    float roughness = noiseSimplexFbm2D(q * 11.0 + phase, site.key ^ 0x314bu, 3, 2.03, 0.48);
    return site.height * min(exterior, bowl) * rimVariation
        * (1.0 - 0.18 * gullies * flank + 0.025 * roughness * flank);
}

float volcanicMountainHeight(vec2 p)
{
    ivec2 cell = ivec2(floor(p / VOLCANIC_CELL_SIZE));
    float h = 0.0;
    // Radius may cross a cell boundary. Evaluating neighbours prevents cuts
    // there; max also prevents double-height peaks where two feet overlap.
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 candidate = cell + ivec2(x, y);
        // Reject the whole possible centre rectangle before hashing or noise.
        vec2 low = (vec2(candidate) + 0.25) * VOLCANIC_CELL_SIZE;
        vec2 high = low + 0.5 * VOLCANIC_CELL_SIZE;
        vec2 separation = max(max(low - p, p - high), vec2(0.0));
        float reach = 1.5 * VOLCANIC_RADIUS_RANGE.y;
        if (dot(separation, separation) > reach * reach) continue;
        h = max(h, volcanicConeHeight(p, volcanicSiteInCell(candidate)));
    }
    return h;
}

#endif
