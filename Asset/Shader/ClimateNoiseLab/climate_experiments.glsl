// Adjustable climate-lab reference and experimental shaping. The terrain uses
// the fixed validated climate recipe; unrelated experiments stay in this file.
#ifndef ENDURANCE_CLIMATE_EXPERIMENTS_GLSL
#define ENDURANCE_CLIMATE_EXPERIMENTS_GLSL

#include "../Noise/noise_2d.glsl"

// Five complete noises with independent seeds. transition is a half-width in
// noise-value units, not pixels or metres. Zero preserves the strict >0 test.
float climateLabSum5(vec2 p, uint seed, int octaves,
                     float lacunarity, float gain, float transition)
{
    float sum = 0.0;
    for (uint layer = 0u; layer < 5u; ++layer)
    {
        uint layerSeed = noiseInternalHash(seed ^ ((layer + 1u) * 0x9e3779b9u));
        float value = noiseSimplexFbm2D(p, layerSeed, octaves, lacunarity, gain);
        sum += transition > 0.0
            ? smoothstep(-transition, transition, value)
            : (value > 0.0 ? 1.0 : 0.0);
    }
    return sum * (1.0 / 5.0);
}

float climateLabAttractNearFifths01(float value, float target, float maxShift)
{
    value = clamp(value, 0.0, 1.0);
    target = clamp(target, 0.0, 1.0);
    float scaled = value * 5.0;
    float distance = abs(scaled - floor(scaled + 0.5));
    float factor = 1.0 - distance * 2.0;
    float limit = clamp(maxShift, 0.0, 1.0) * 5.0 * factor;
    float delta = clamp(target * 5.0 - scaled, -limit, limit);
    return clamp((scaled + delta) / 5.0, 0.0, 1.0);
}

float attractNearestMatrixLevel01(float value, float maxShift)
{
    value = clamp(value, 0.0, 1.0);
    if (maxShift <= 0.0) return value;

    // Blend the two possible targets around their midpoint instead of round(),
    // which jumps by up to 2*maxShift. Outside this band the target is unchanged.
    float scaled = value * 4.0;
    float band = clamp(maxShift * 4.0, 0.000001, 0.5);
    float target = (floor(scaled) + smoothstep(0.5 - band, 0.5 + band,
                                             fract(scaled))) * 0.25;
    return clamp(value + clamp(target - value, -maxShift, maxShift), 0.0, 1.0);
}

#endif
