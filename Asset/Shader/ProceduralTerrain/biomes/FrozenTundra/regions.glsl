#ifndef ENDURANCE_FROZEN_REGIONS_GLSL
#define ENDURANCE_FROZEN_REGIONS_GLSL
#include "../../biome_landforms_2d.glsl"

// Region scale and nominal transition span, in metres. A regularized local
// gradient converts the noise to an approximate distance before the fade.
const float FROZEN_REGION_SCALE = 38000.0;
const float FROZEN_REGION_TRANSITION = 6500.0;
const float FROZEN_REGION_THRESHOLD = 0.02;
// Preserve the user's latest raw-height multiplier (commit c6dbcab).
const float FROZEN_MOUNTAIN_HEIGHT_GAIN = 2.0;

float frozenRegionSignal(vec2 p)
{
    // Broad bending plus a second displaced scale prevents the outline from
    // tracing the lobes of a single simple noise. All terms are continuous.
    vec2 broad = vec2(noiseSimplex2D(p / (FROZEN_REGION_SCALE * 1.214), uSeed ^ 310u),
        noiseSimplex2D(p / (FROZEN_REGION_SCALE * 1.214), uSeed ^ 311u));
    vec2 q = p + broad * (FROZEN_REGION_SCALE * 0.20);
    vec2 secondary = vec2(noiseSimplex2D(q / (FROZEN_REGION_SCALE * 0.375), uSeed ^ 312u),
        noiseSimplex2D(q / (FROZEN_REGION_SCALE * 0.375), uSeed ^ 313u));
    q += secondary * (FROZEN_REGION_SCALE * 0.043);
    return 0.84 * noiseSimplex2D(q / FROZEN_REGION_SCALE, uSeed ^ 314u)
        + 0.16 * noiseSimplex2D(q / (FROZEN_REGION_SCALE * 0.41), uSeed ^ 315u);
}

float frozenMountainPresence(vec2 p)
{
    float region = frozenRegionSignal(p) - FROZEN_REGION_THRESHOLD;
    float stepSize = FROZEN_REGION_SCALE * 0.005;
    vec2 gradient = vec2(
        frozenRegionSignal(p + vec2(stepSize, 0.0)) - frozenRegionSignal(p - vec2(stepSize, 0.0)),
        frozenRegionSignal(p + vec2(0.0, stepSize)) - frozenRegionSignal(p - vec2(0.0, stepSize))) / (2.0 * stepSize);
    // A fixed noise-value ramp compressed to ~1 km on steep noise gradients.
    // Normalize that gradient so the border instead fades across kilometres.
    // Smooth regularization avoids division spikes or a hard gradient clamp.
    float floorGradient = 0.25 / FROZEN_REGION_SCALE;
    float distance = region / sqrt(dot(gradient, gradient) + floorGradient * floorGradient);
    float t = clamp(0.5 + distance / max(FROZEN_REGION_TRANSITION, 1.0), 0.0, 1.0);
    // C2 endpoints: neither the slope nor its derivative jumps where a plain
    // starts or a full-height mountain region ends. No thresholded height cut.
    return clamp(noiseFadeQuintic(t), 0.0, 1.0);
}

// Metres at reference detailScale=.35. The same substrate continues below
// the mountains, so no separate plain/mountain height seam is introduced.
float frozenPlainHeight(vec2 p)
{
    vec2 q = landWarp(p, 6400.0, 420.0, 316u);
    float broad = noiseSimplexFbm2D(q / 5400.0, uSeed ^ 317u, 2, 2.03, 0.42);
    float rolls = noiseSimplex2D(q / 1600.0, uSeed ^ 318u);
    vec2 direction = landSparseDirection(319u);
    vec2 wind = vec2(dot(q, direction), dot(q, vec2(-direction.y, direction.x)));
    float hummocks = noiseSimplex2D(wind / vec2(380.0, 1400.0), uSeed ^ 320u);
    float basin = 0.5 + 0.5 * noiseSimplex2D(q / 2100.0, uSeed ^ 321u);
    basin = noiseFadeQuintic(clamp((basin - 0.62) / 0.30, 0.0, 1.0));
    float fine = noiseSimplexFbm2D(q / 95.0, uSeed ^ 322u, 2, 2.1, 0.40);
    float grains = noiseSimplex2D(q / 32.0, uSeed ^ 323u);
    return 14.0 + broad * 32.0 + rolls * 7.0 + hummocks * 2.1
        - basin * 6.0 + fine * 0.80 + grains * 0.25;
}
#endif
