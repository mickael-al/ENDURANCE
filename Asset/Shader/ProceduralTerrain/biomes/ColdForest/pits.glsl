#ifndef ENDURANCE_COLD_FOREST_PITS_GLSL
#define ENDURANCE_COLD_FOREST_PITS_GLSL
#include "ground.glsl"
#include "pit_settings.glsl"

struct ColdForestPitSite { vec2 center; uint key; };

ColdForestPitSite coldForestPitInCell(ivec2 cell)
{
    uint key = noiseInternalHash(uint(cell.x) * 0x9e3779b9u
        ^ uint(cell.y) * 0x85ebca6bu ^ uSeed ^ 0xc084b9d3u);
    vec2 jitter = vec2(noiseInternalUintTo01(noiseInternalHash(key ^ 0x7f4a7c15u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x94d049bbu)));
    return ColdForestPitSite((vec2(cell) + 0.25 + 0.5 * jitter)
        * COLD_FOREST_PIT_SPACING, key);
}

ColdForestPitSite coldForestPitSite(vec2 p)
{
    return coldForestPitInCell(ivec2(floor(p / COLD_FOREST_PIT_SPACING)));
}

float coldForestPitEligibility(ColdForestPitSite site)
{
    // Keep the original small anchor clearance, not a 20-km disk veto.
    // Regional presence and river protection are applied locally below.
    return coldForestRiverDiskClear(site.center, COLD_FOREST_PIT_ANCHOR_RADIUS)
        ? 1.0 : 0.0;
}

float coldForestPitLocalMask(vec2 p)
{
    float riverProtection = smoothstep(COLD_FOREST_RIVER_BANK_HALF_WIDTH,
        COLD_FOREST_RIVER_BANK_HALF_WIDTH + COLD_FOREST_PIT_RIVER_FADE,
        coldForestRiverDistance(p));
    if (riverProtection <= 0.0) return 0.0;
    return coldForestRegionMask(p) * riverProtection;
}

float coldForestPitOffset(vec2 p)
{
    float offset = uBandOffset;
    if (uBandOffsetVariation > 0.0)
        offset += uBandOffsetVariation * noisePerlin2D(p * 0.0004, uSeed ^ 0xa4093822u);
    return offset;
}

float coldForestPitMaximumDepth(vec2 p)
{
    return max(0.0, 0.5 * FIELD_MAX_VOLUME_THICKNESS * uDetailScale
        - COLD_FOREST_PIT_BAND_MARGIN - abs(coldForestPitOffset(p)))
        * clamp(COLD_FOREST_PIT_DEPTH_SCALE, 0.0, 1.0);
}

float coldForestPitDepth(vec2 p, ColdForestPitSite site, float maximumDepth)
{
    vec2 q = p - site.center;
    float radial = length(q) / COLD_FOREST_PIT_RADIUS;
    if (radial >= 1.0) return 0.0;
    float inward = 1.0 - radial;
    float depth = maximumDepth * inward;
    if (COLD_FOREST_PIT_SPIRAL_DEPTH > 0.0 && radial > 0.00001)
    {
        float phase = noiseInternalUintTo01(noiseInternalHash(site.key ^ 0x52682e93u))
            * 6.28318530718;
        float handedness = (site.key & 1u) == 0u ? -1.0 : 1.0;
        float spiral = sin(handedness * atan(q.y, q.x) + phase
            + radial * (6.28318530718 * COLD_FOREST_PIT_SPIRAL_TURNS));
        depth += COLD_FOREST_PIT_SPIRAL_DEPTH * 4.0 * radial * inward * spiral;
    }
    float rim = smoothstep(0.0, COLD_FOREST_PIT_RIM_WIDTH / COLD_FOREST_PIT_RADIUS,
        inward);
    return clamp(depth * rim, 0.0, maximumDepth);
}

float coldForestPitDepth(vec2 p, ColdForestPitSite site)
{
    return coldForestPitDepth(p, site, coldForestPitMaximumDepth(p));
}

float coldForestPitVoidFraction(float radial01)
{
    return clamp((1.0 - radial01) / max(1.0 - COLD_FOREST_PIT_EMPTY_RADIUS, 0.001),
        0.0, 1.0);
}

// World depth and void fraction. Both maxima are continuous across overlaps;
// no single winning site/key is carried into the 3D noise.
vec2 coldForestPitFields(vec2 p)
{
    if (uFieldMode != 0u || uDetailScale <= 0.0) return vec2(0.0);
    float mask = coldForestPitLocalMask(p);
    if (mask <= 0.0) return vec2(0.0);
    float maximumDepth = coldForestPitMaximumDepth(p);
    if (maximumDepth <= 0.0) return vec2(0.0);
    ivec2 cell = ivec2(floor(p / COLD_FOREST_PIT_SPACING));
    vec2 fields = vec2(0.0);
    // A 10-km radius on 10-km cells reaches only these nine candidates.
    // The first omitted cell's closest possible centre is 12.5 km away.
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ColdForestPitSite site = coldForestPitInCell(cell + ivec2(x, z));
        float radial = length(p - site.center) / COLD_FOREST_PIT_RADIUS;
        if (radial >= 1.0 || coldForestPitEligibility(site) <= 0.0) continue;
        fields = max(fields, vec2(coldForestPitDepth(p, site, maximumDepth),
            coldForestPitVoidFraction(radial)));
    }
    return fields * mask;
}

float coldForestPitBand(vec2 p, float depth)
{
    if (depth <= 0.0 || uDetailScale <= 0.0) return 0.0;
    // Band stores depth at better than 5-cm resolution at detailScale=.35.
    // The separate pilot remains available for the independently unioned pores.
    float halfBand = abs(coldForestPitOffset(p)) + depth + COLD_FOREST_PIT_BAND_MARGIN;
    return min(2.0 * halfBand / uDetailScale, FIELD_MAX_VOLUME_THICKNESS);
}

#endif
