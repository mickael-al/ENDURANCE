// Fixed scalar pattern network shared by the preview and GPU genetic trainer.
// The including shader declares an SSBO float array named
// `neuralPatternWeights`; weightBase selects one individual in that array.
#ifndef ENDURANCE_NEURAL_PATTERN_2D_GLSL
#define ENDURANCE_NEURAL_PATTERN_2D_GLSL

#include "noise_2d.glsl"

#define NEURAL_PATTERN_INPUT_COUNT 12
#define NEURAL_PATTERN_WIDTH 8
#define NEURAL_PATTERN_HIDDEN_LAYERS 4
#define NEURAL_PATTERN_PARAMETER_COUNT 329

#define NEURAL_PATTERN_W0_OFFSET 0
#define NEURAL_PATTERN_B0_OFFSET 96
#define NEURAL_PATTERN_W1_OFFSET 104
#define NEURAL_PATTERN_B1_OFFSET 168
#define NEURAL_PATTERN_W2_OFFSET 176
#define NEURAL_PATTERN_B2_OFFSET 240
#define NEURAL_PATTERN_W3_OFFSET 248
#define NEURAL_PATTERN_B3_OFFSET 312
#define NEURAL_PATTERN_WO_OFFSET 320
#define NEURAL_PATTERN_BO_OFFSET 328

// Twelve stable inputs: two periodic Fourier bands per axis and four Perlin
// values in unwrapped coordinates. Fourier reproduces the reference period;
// Perlin provides deterministic variation outside that first period.
void noiseNeuralPatternFeatures2D(vec2 p, uint seed,
                                  out float features[NEURAL_PATTERN_INPUT_COUNT])
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

float noiseNeuralPatternWeight(uint weightBase, int index)
{
    return neuralPatternWeights[weightBase + uint(index)];
}

// Fixed 12 -> 8 -> 8 -> 8 -> 8 -> 1 MLP. The four hidden layers contain
// exactly 32 tanh operations. The scalar output is remapped and clamped to
// [0,1], so one trained model behaves like an ordinary scalar noise.
float noiseNeuralPattern2D01(vec2 p, uint seed, uint weightBase)
{
    float inputValues[NEURAL_PATTERN_INPUT_COUNT];
    float layerA[NEURAL_PATTERN_WIDTH];
    float layerB[NEURAL_PATTERN_WIDTH];
    noiseNeuralPatternFeatures2D(p, seed, inputValues);

    for (int outputIndex = 0; outputIndex < NEURAL_PATTERN_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralPatternWeight(weightBase,
            NEURAL_PATTERN_B0_OFFSET + outputIndex);
        int weights = NEURAL_PATTERN_W0_OFFSET
                    + outputIndex * NEURAL_PATTERN_INPUT_COUNT;
        for (int inputIndex = 0; inputIndex < NEURAL_PATTERN_INPUT_COUNT; ++inputIndex)
            value += noiseNeuralPatternWeight(weightBase, weights + inputIndex)
                   * inputValues[inputIndex];
        layerA[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_PATTERN_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralPatternWeight(weightBase,
            NEURAL_PATTERN_B1_OFFSET + outputIndex);
        int weights = NEURAL_PATTERN_W1_OFFSET + outputIndex * NEURAL_PATTERN_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_PATTERN_WIDTH; ++inputIndex)
            value += noiseNeuralPatternWeight(weightBase, weights + inputIndex)
                   * layerA[inputIndex];
        layerB[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_PATTERN_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralPatternWeight(weightBase,
            NEURAL_PATTERN_B2_OFFSET + outputIndex);
        int weights = NEURAL_PATTERN_W2_OFFSET + outputIndex * NEURAL_PATTERN_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_PATTERN_WIDTH; ++inputIndex)
            value += noiseNeuralPatternWeight(weightBase, weights + inputIndex)
                   * layerB[inputIndex];
        layerA[outputIndex] = tanh(value);
    }

    for (int outputIndex = 0; outputIndex < NEURAL_PATTERN_WIDTH; ++outputIndex)
    {
        float value = noiseNeuralPatternWeight(weightBase,
            NEURAL_PATTERN_B3_OFFSET + outputIndex);
        int weights = NEURAL_PATTERN_W3_OFFSET + outputIndex * NEURAL_PATTERN_WIDTH;
        for (int inputIndex = 0; inputIndex < NEURAL_PATTERN_WIDTH; ++inputIndex)
            value += noiseNeuralPatternWeight(weightBase, weights + inputIndex)
                   * layerA[inputIndex];
        layerB[outputIndex] = tanh(value);
    }

    float outputValue = noiseNeuralPatternWeight(weightBase,
        NEURAL_PATTERN_BO_OFFSET);
    for (int inputIndex = 0; inputIndex < NEURAL_PATTERN_WIDTH; ++inputIndex)
        outputValue += noiseNeuralPatternWeight(weightBase,
            NEURAL_PATTERN_WO_OFFSET + inputIndex) * layerB[inputIndex];
    return clamp(0.5 + 0.5 * outputValue, 0.0, 1.0);
}

float noiseNeuralPattern2D(vec2 p, uint seed, uint weightBase)
{
    return noiseNeuralPattern2D01(p, seed, weightBase) * 2.0 - 1.0;
}

#endif
