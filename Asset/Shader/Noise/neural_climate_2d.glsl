// Compact coordinate-network climate field.
// The trainer and this runtime share the same 16 Fourier/Perlin features.
#ifndef ENDURANCE_NEURAL_CLIMATE_2D_GLSL
#define ENDURANCE_NEURAL_CLIMATE_2D_GLSL

#include "noise_2d.glsl"
#include "neural_climate_weights.glsl"

#if NEURAL_CLIMATE_INPUT_COUNT != 16
#error neural_climate_2d.glsl expects the 16-feature exporter contract
#endif
#if NEURAL_CLIMATE_OUTPUT_COUNT != 2
#error Neural Climate must export humidity and temperature
#endif

void noiseNeuralClimateFeatures2D(vec2 p, uint seed,
                                  out float features[NEURAL_CLIMATE_INPUT_COUNT])
{
    const float tau = 6.28318530717958647692;
    vec2 phase1 = p * tau;
    vec2 phase2 = p * (2.0 * tau);
    vec2 phase4 = p * (4.0 * tau);

    features[0] = sin(phase1.x);
    features[1] = cos(phase1.x);
    features[2] = sin(phase1.y);
    features[3] = cos(phase1.y);
    features[4] = sin(phase2.x);
    features[5] = cos(phase2.x);
    features[6] = sin(phase2.y);
    features[7] = cos(phase2.y);
    features[8] = sin(phase4.x);
    features[9] = cos(phase4.x);
    features[10] = sin(phase4.y);
    features[11] = cos(phase4.y);

    // Absolute, unwrapped coordinates drive these features. Fourier coordinates
    // repeat every reference tile, while Perlin changes between tiles and gives
    // the trained mapping a deterministic source of infinite variation.
    features[12] = noisePerlin2D(p,       seed ^ 0x243f6a88u);
    features[13] = noisePerlin2D(p * 2.0, seed ^ 0x85a308d3u);
    features[14] = noisePerlin2D(p * 4.0, seed ^ 0x13198a2eu);
    features[15] = noisePerlin2D(p * 8.0, seed ^ 0x03707344u);
}

float noiseNeuralClimateSigmoid(float value)
{
    value = clamp(value, -20.0, 20.0);
    return 1.0 / (1.0 + exp(-value));
}

// Returns x=humidity and y=temperature in [0,1]. The weights are constants
// generated offline; runtime evaluation has no texture lookup or mutable state.
vec2 noiseNeuralClimate2D01(vec2 p, uint seed)
{
    float inputValues[NEURAL_CLIMATE_INPUT_COUNT];
    float hidden0[NEURAL_CLIMATE_HIDDEN0_COUNT];
    float hidden1[NEURAL_CLIMATE_HIDDEN1_COUNT];
    float outputValues[NEURAL_CLIMATE_OUTPUT_COUNT];
    noiseNeuralClimateFeatures2D(p, seed, inputValues);

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_HIDDEN0_COUNT; ++outputIndex)
    {
        float value = NEURAL_CLIMATE_B0[outputIndex];
        int weightBase = outputIndex * NEURAL_CLIMATE_INPUT_COUNT;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_INPUT_COUNT; ++inputIndex)
            value += NEURAL_CLIMATE_W0[weightBase + inputIndex] * inputValues[inputIndex];
        hidden0[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_HIDDEN1_COUNT; ++outputIndex)
    {
        float value = NEURAL_CLIMATE_B1[outputIndex];
        int weightBase = outputIndex * NEURAL_CLIMATE_HIDDEN0_COUNT;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_HIDDEN0_COUNT; ++inputIndex)
            value += NEURAL_CLIMATE_W1[weightBase + inputIndex] * hidden0[inputIndex];
        hidden1[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_OUTPUT_COUNT; ++outputIndex)
    {
        float value = NEURAL_CLIMATE_B2[outputIndex];
        int weightBase = outputIndex * NEURAL_CLIMATE_HIDDEN1_COUNT;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_HIDDEN1_COUNT; ++inputIndex)
            value += NEURAL_CLIMATE_W2[weightBase + inputIndex] * hidden1[inputIndex];
        outputValues[outputIndex] = noiseNeuralClimateSigmoid(value);
    }

    return clamp(vec2(outputValues[0], outputValues[1]), 0.0, 1.0);
}

vec2 noiseNeuralClimate2D(vec2 p, uint seed)
{
    return noiseNeuralClimate2D01(p, seed) * 2.0 - 1.0;
}

#endif
