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
    // 1000-m footprint inside the cell's 2500-m safe margin: no neighbour loop.
    return coldForestPitInCell(ivec2(floor(p / COLD_FOREST_PIT_SPACING)));
}

float coldForestPitEligibility(ColdForestPitSite site)
{
    if (!coldForestRiverDiskClear(site.center, COLD_FOREST_PIT_RADIUS + 100.0))
        return 0.0;
    if (coldForestRegionMask(site.center) < COLD_FOREST_PIT_REGION_MIN)
        return 0.0;
    // Accept a whole pit; never clip individual pieces with the regional mask.
    // Dense disk validation also checks between these padded perimeter samples.
    for (int i = 0; i < 8; ++i)
    {
        float angle = float(i) * (6.28318530718 / 8.0);
        vec2 p = site.center + (COLD_FOREST_PIT_RADIUS + 200.0)
            * vec2(cos(angle), sin(angle));
        if (coldForestRegionMask(p) < COLD_FOREST_PIT_REGION_MIN) return 0.0;
    }
    return 1.0;
}

float coldForestPitDepth(vec2 p, ColdForestPitSite site)
{
    vec2 q = p - site.center;
    float radial = length(q) / COLD_FOREST_PIT_RADIUS;
    if (radial >= 1.0) return 0.0;
    float inward = 1.0 - radial;
    float depth = COLD_FOREST_PIT_DEPTH * inward;
    if (COLD_FOREST_PIT_SPIRAL_DEPTH > 0.0 && radial > 0.00001)
    {
        float phase = noiseInternalUintTo01(noiseInternalHash(site.key ^ 0x52682e93u))
            * 6.28318530718;
        float handedness = (site.key & 1u) == 0u ? -1.0 : 1.0;
        // Broad inward/downward spiral. One evaluation per XZ column, not a
        // repeated 3D search for the nearest point on a descending torus.
        float spiral = sin(handedness * atan(q.y, q.x) + phase
            + radial * (6.28318530718 * COLD_FOREST_PIT_SPIRAL_TURNS));
        depth += COLD_FOREST_PIT_SPIRAL_DEPTH * 4.0 * radial * inward * spiral;
    }
    float rim = smoothstep(0.0, COLD_FOREST_PIT_RIM_WIDTH / COLD_FOREST_PIT_RADIUS,
        inward);
    return clamp(depth * rim, 0.0, COLD_FOREST_PIT_DEPTH);
}

float coldForestPitVoidFraction(float radial01)
{
    return clamp(1.0 - radial01, 0.0, 1.0);
}

float coldForestPitControl(vec2 p)
{
    if (uFieldMode != 0u || uDetailScale <= 0.0) return 0.0;
    ColdForestPitSite site = coldForestPitSite(p);
    if (length(p - site.center) >= COLD_FOREST_PIT_RADIUS) return 0.0;
    if (coldForestPitEligibility(site) <= 0.0) return 0.0;
    return coldForestPitDepth(p, site) / COLD_FOREST_PIT_DEPTH;
}

float coldForestPitBand(vec2 p, float control)
{
    if (control <= 0.0 || uDetailScale <= 0.0) return 0.0;
    float offset = uBandOffset;
    if (uBandOffsetVariation > 0.0)
        offset += uBandOffsetVariation * noisePerlin2D(p * 0.0004, uSeed ^ 0xa4093822u);
    // Full floor plus fade/storage margin at this XZ column.
    float halfBand = abs(offset) + control * COLD_FOREST_PIT_DEPTH + 96.0;
    return min(2.0 * halfBand / uDetailScale, FIELD_MAX_VOLUME_THICKNESS);
}

#endif
