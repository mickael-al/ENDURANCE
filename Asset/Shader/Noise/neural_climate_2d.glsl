// Fixed climate network shared by the preview and the GPU genetic trainer.
// The including shader must declare an SSBO float array named
// `neuralClimateWeights`. `weightBase` selects one individual in that array.
#ifndef ENDURANCE_NEURAL_CLIMATE_2D_GLSL
#define ENDURANCE_NEURAL_CLIMATE_2D_GLSL

#include "noise_2d.glsl"

#define NEURAL_CLIMATE_INPUT_COUNT 12
#define NEURAL_CLIMATE_WIDTH 8
#define NEURAL_CLIMATE_HIDDEN_LAYERS 4
#define NEURAL_CLIMATE_PARAMETER_COUNT 338

#define NEURAL_CLIMATE_W0_OFFSET 0
#define NEURAL_CLIMATE_B0_OFFSET 96
#define NEURAL_CLIMATE_W1_OFFSET 104
#define NEURAL_CLIMATE_B1_OFFSET 168
#define NEURAL_CLIMATE_W2_OFFSET 176
#define NEURAL_CLIMATE_B2_OFFSET 240
#define NEURAL_CLIMATE_W3_OFFSET 248
#define NEURAL_CLIMATE_B3_OFFSET 312
#define NEURAL_CLIMATE_WO_OFFSET 320
#define NEURAL_CLIMATE_BO_OFFSET 336

// Twelve stable inputs: two periodic Fourier bands per axis and four Perlin
// values evaluated in unwrapped coordinates. The Fourier part can reproduce the
// 2048x2048 reference tile; Perlin supplies deterministic variation outside it.
void noiseNeuralClimateFeatures2D(vec2 p, uint seed,
                                  out float features[NEURAL_CLIMATE_INPUT_COUNT])
{
    const float tau = 6.28318530717958647692;
    vec2 tile = fract(p);
    vec2 phase1 = tile * tau;
    vec2 phase2 = tile * (2.0 * tau);

    features[0] = sin(phase1.x);
    features[1] = cos(phase1.x);
    features[2] = sin(phase1.y);
    features[3] = cos(phase1.y);
    features[4] = sin(phase2.x);
    features[5] = cos(phase2.x);
    features[6] = sin(phase2.y);
    features[7] = cos(phase2.y);

    features[8]  = noisePerlin2D(p + vec2(11.7, -5.3),
                                  seed ^ 0x243f6a88u);
    features[9]  = noisePerlin2D(p * 2.0 + vec2(-7.1, 19.4),
                                  seed ^ 0x85a308d3u);
    features[10] = noisePerlin2D(p * 4.0 + vec2(31.2, 8.6),
                                  seed ^ 0x13198a2eu);
    features[11] = noisePerlin2D(p * 8.0 + vec2(-23.8, -14.5),
                                  seed ^ 0x03707344u);
}

float noiseNeuralClimateWeight(uint weightBase, int index)
{
    return neuralClimateWeights[weightBase + uint(index)];
}

// Fixed 12 -> 8 -> 8 -> 8 -> 8 -> 2 MLP. The four hidden layers contain
// exactly 32 tanh operations. The output layer is linear and remapped/clamped
// to [0,1], so the six unused neurons of a direct vec2 output are avoided.
vec2 noiseNeuralClimate2D01(vec2 p, uint seed, uint weightBase)
{
    float inputValues[NEURAL_CLIMATE_INPUT_COUNT];
    float layerA[NEURAL_CLIMATE_WIDTH];
    float layerB[NEURAL_CLIMATE_WIDTH];
    noiseNeuralClimateFeatures2D(p, seed, inputValues);

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralClimateWeight(weightBase,
            NEURAL_CLIMATE_B0_OFFSET + outputIndex);
        int weights = NEURAL_CLIMATE_W0_OFFSET
                    + outputIndex * NEURAL_CLIMATE_INPUT_COUNT;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_INPUT_COUNT; ++inputIndex)
            value += noiseNeuralClimateWeight(weightBase, weights + inputIndex)
                   * inputValues[inputIndex];
        layerA[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralClimateWeight(weightBase,
            NEURAL_CLIMATE_B1_OFFSET + outputIndex);
        int weights = NEURAL_CLIMATE_W1_OFFSET + outputIndex * NEURAL_CLIMATE_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_WIDTH; ++inputIndex)
            value += noiseNeuralClimateWeight(weightBase, weights + inputIndex)
                   * layerA[inputIndex];
        layerB[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralClimateWeight(weightBase,
            NEURAL_CLIMATE_B2_OFFSET + outputIndex);
        int weights = NEURAL_CLIMATE_W2_OFFSET + outputIndex * NEURAL_CLIMATE_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_WIDTH; ++inputIndex)
            value += noiseNeuralClimateWeight(weightBase, weights + inputIndex)
                   * layerB[inputIndex];
        layerA[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_CLIMATE_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralClimateWeight(weightBase,
            NEURAL_CLIMATE_B3_OFFSET + outputIndex);
        int weights = NEURAL_CLIMATE_W3_OFFSET + outputIndex * NEURAL_CLIMATE_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_WIDTH; ++inputIndex)
            value += noiseNeuralClimateWeight(weightBase, weights + inputIndex)
                   * layerA[inputIndex];
        layerB[outputIndex] = tanh(value);
    }

    vec2 outputValues;
    for (int outputIndex = 0; outputIndex < 2; ++outputIndex)
    {
        float value = noiseNeuralClimateWeight(weightBase,
            NEURAL_CLIMATE_BO_OFFSET + outputIndex);
        int weights = NEURAL_CLIMATE_WO_OFFSET + outputIndex * NEURAL_CLIMATE_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_CLIMATE_WIDTH; ++inputIndex)
            value += noiseNeuralClimateWeight(weightBase, weights + inputIndex)
                   * layerB[inputIndex];
        outputValues[outputIndex] = clamp(0.5 + 0.5 * value, 0.0, 1.0);
    }

    // x = humidity, y = temperature.
    return outputValues;
}

vec2 noiseNeuralClimate2D(vec2 p, uint seed, uint weightBase)
{
    return noiseNeuralClimate2D01(p, seed, weightBase) * 2.0 - 1.0;
}

#endif
