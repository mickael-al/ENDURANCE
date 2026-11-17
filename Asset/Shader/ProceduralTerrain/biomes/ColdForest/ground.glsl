#ifndef ENDURANCE_COLD_FOREST_GROUND_GLSL
#define ENDURANCE_COLD_FOREST_GROUND_GLSL
#include "../../biome_landforms_2d.glsl"
#include "ground_settings.glsl"

float coldForestRegionMask(vec2 p)
{
    vec2 q = p / COLD_FOREST_REGION_SCALE;
    q += 0.18 * vec2(noiseSimplex2D(q * 0.73, uSeed ^ 810u),
        noiseSimplex2D(q * 0.73, uSeed ^ 811u));
    float region = 0.82 * noiseSimplex2D(q, uSeed ^ 812u)
        + 0.18 * noiseSimplex2D(q * 2.11 + vec2(17.0, 29.0), uSeed ^ 813u);
    float t = clamp((region - COLD_FOREST_REGION_LOW)
        / (COLD_FOREST_REGION_HIGH - COLD_FOREST_REGION_LOW), 0.0, 1.0);
    // C2 endpoints keep both the ground slope and its derivative continuous
    // when river regions fade into ordinary ground over several kilometres.
    return clamp(noiseFadeQuintic(t), 0.0, 1.0);
}

float coldForestRiverDistance(vec2 p)
{
    vec2 q = noiseRotate2D(p, vec2(0.6, 0.8));
    // A bounded meander provides an inexpensive conservative exclusion test
    // for an entire 2-km pit footprint, including between sampled rim points.
    float meander = COLD_FOREST_RIVER_MEANDER * clamp(noiseSimplex2D(
        vec2(17.0, q.y / COLD_FOREST_RIVER_MEANDER_SCALE), uSeed ^ 802u), -1.0, 1.0);
    float phase = (q.x + meander) / COLD_FOREST_RIVER_SPACING;
    return abs(fract(phase + 0.5) - 0.5) * COLD_FOREST_RIVER_SPACING;
}

float coldForestRiverChannelMask(vec2 p)
{
    float t = clamp((coldForestRiverDistance(p) - COLD_FOREST_RIVER_FLOOR_HALF_WIDTH)
        / (COLD_FOREST_RIVER_BANK_HALF_WIDTH - COLD_FOREST_RIVER_FLOOR_HALF_WIDTH), 0.0, 1.0);
    return clamp(1.0 - noiseFadeQuintic(t), 0.0, 1.0);
}

float coldForestRiverMask(vec2 p)
{
    return coldForestRegionMask(p) * coldForestRiverChannelMask(p);
}

bool coldForestRiverDiskClear(vec2 center, float radius)
{
    // Rotated X changes by at most radius across a disk. The bounded
    // meander changes by at most twice its amplitude. The periodic distance
    // is 1-Lipschitz, so this guarantees no channel intersects the disk.
    return coldForestRiverDistance(center) > max(radius, 0.0)
        + 2.0 * COLD_FOREST_RIVER_MEANDER + COLD_FOREST_RIVER_BANK_HALF_WIDTH;
}

void coldForestGroundTerms(vec2 p, out float baseHeight, out float riverDepth)
{
    vec2 q = noiseRotate2D(landWarp(p, 3500.0, 260.0, 801u), vec2(0.6, 0.8));
    float ridges = landRidges(q * vec2(0.0009, 0.0015), 803u);
    float region = 0.5 + 0.5 * noiseSimplex2D(q * 0.00016, uSeed ^ 804u);
    riverDepth = 240.0 + 160.0 * region + 95.0 * ridges * ridges;

    // Keep the original soil's FBM, micro detail, phase and amplitudes. The
    // old channel mask remains only in this sub-metre surface roughness.
    float oldAxis = q.x * 0.0021 + 0.7
        * noiseSimplex2D(q * vec2(0.00021, 0.00065), uSeed ^ 802u);
    float oldWall = smoothstep(0.18, 0.78, abs(sin(oldAxis)));
    float detail = landFbm(q * vec2(0.00048, 0.00024), 805u) * 24.0;
    detail += noiseSimplex2D(q * 0.007, uSeed ^ 806u) * 1.8 * oldWall;
    baseHeight = -108.0 + riverDepth + detail;
}

float coldForestBaseHeight(vec2 p)
{
    float baseHeight, riverDepth;
    coldForestGroundTerms(p, baseHeight, riverDepth);
    return baseHeight;
}

float coldForestGroundHeight(vec2 p)
{
    float baseHeight, riverDepth;
    coldForestGroundTerms(p, baseHeight, riverDepth);
    // River presence can only lower the substrate. The old localized
    // crescent mound and its alcove are deliberately absent from this layer.
    return baseHeight - riverDepth * coldForestRiverMask(p);
}

#endif
