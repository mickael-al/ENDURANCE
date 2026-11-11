#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Two population halves are stored in one buffer. uSourceIndex selects the
// current half; evolution writes the other half before the CPU toggles it.
layout(std430, binding = 0) buffer GeneticPopulationBuffer
{
    float neuralPatternWeights[];
};
layout(std430, binding = 1) buffer GeneticScoreBuffer
{
    float geneticScores[];
};
layout(std430, binding = 2) buffer GeneticBestBuffer
{
    float geneticBestWeights[];
};
layout(std430, binding = 3) buffer GeneticStateBuffer
{
    float geneticBestScore;
    uint geneticBestGeneration;
    uint geneticGeneration;
    uint geneticSourceIndex;
};

#include "../Noise/neural_pattern_2d.glsl"

layout(binding = 0) uniform sampler2D uReferenceImage;

uniform uint uPass;              // 0=initialise, 1=crop score, 2=evolve, 3=validate
uniform uint uPopulationSize;    // <= 256
uniform uint uSourceIndex;       // 0 or 1
uniform uint uGeneration;
uniform uint uSeed;
uniform ivec2 uReferenceSize;
uniform ivec2 uCropOffset;
uniform int uCropSize;
uniform uint uSampleGrid;
uniform float uMutationScale;
uniform int uReferenceChannel;   // 0=R, 1=G, 2=B, 3=luminance

#define NEURAL_GENETIC_MAX_POPULATION 256
#define NEURAL_GENETIC_VALIDATION_GRID 32u

shared float reductionValues[64];
shared float sharedScores[NEURAL_GENETIC_MAX_POPULATION];
shared uint sharedBestIndex;

uint geneticHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float geneticRandom01(uint value)
{
    return float(geneticHash(value) >> 8u) * (1.0 / 16777215.0);
}

float geneticRandomSigned(uint value)
{
    return geneticRandom01(value) * 2.0 - 1.0;
}

uint geneticPopulationBase(uint populationHalf, uint individual)
{
    return (populationHalf * uPopulationSize + individual)
         * uint(NEURAL_PATTERN_PARAMETER_COUNT);
}

void initialisePopulation()
{
    uint index = gl_GlobalInvocationID.x;
    uint parameterCount = uint(NEURAL_PATTERN_PARAMETER_COUNT);
    uint total = uPopulationSize * parameterCount;
    if (index < total)
    {
        uint individual = index / parameterCount;
        uint parameter = index - individual * parameterCount;
        uint randomSeed = uSeed ^ (individual * 0x9e3779b9u)
                        ^ (parameter * 0x85ebca6bu);
        // Individual zero is a neutral 0.5 pattern. The others start with
        // small values so tanh is not immediately saturated.
        neuralPatternWeights[index] = individual == 0u
            ? 0.0 : geneticRandomSigned(randomSeed) * 0.38;
    }
    if (index < uPopulationSize)
        geneticScores[index] = 1.0e30;
    if (index < uint(NEURAL_PATTERN_PARAMETER_COUNT))
        geneticBestWeights[index] = 0.0;
    if (index == 0u)
    {
        geneticBestScore = 1.0e30;
        geneticBestGeneration = 0u;
        geneticGeneration = 0u;
        geneticSourceIndex = 0u;
    }
}

ivec2 geneticSamplePixel(uint sampleIndex)
{
    uint grid = max(uSampleGrid, 1u);
    uint gx = sampleIndex % grid;
    uint gy = sampleIndex / grid;
    int crop = max(uCropSize, 1);

    int beginX = int(gx) * crop / int(grid);
    int endX = max(beginX + 1, int(gx + 1u) * crop / int(grid));
    int beginY = int(gy) * crop / int(grid);
    int endY = max(beginY + 1, int(gy + 1u) * crop / int(grid));

    uint h0 = geneticHash(sampleIndex ^ (uGeneration * 0x9e3779b9u)
                        ^ (uSeed * 0x85ebca6bu));
    uint h1 = geneticHash(h0 ^ 0x68bc21ebu);
    int x = beginX + int(h0 % uint(max(endX - beginX, 1)));
    int y = beginY + int(h1 % uint(max(endY - beginY, 1)));
    return clamp(uCropOffset + ivec2(x, y), ivec2(0), uReferenceSize - 1);
}

float referencePatternValue(vec4 reference)
{
    if (uReferenceChannel == 1) return reference.g;
    if (uReferenceChannel == 2) return reference.b;
    if (uReferenceChannel == 3)
        return dot(reference.rgb, vec3(0.2126, 0.7152, 0.0722));
    return reference.r;
}

float geneticPatternError(ivec2 pixel, uint base)
{
    vec2 p = (vec2(pixel) + 0.5) / vec2(max(uReferenceSize, ivec2(1)));
    float target = referencePatternValue(texelFetch(uReferenceImage, pixel, 0));
    float predicted = noiseNeuralPattern2D01(p, uSeed, base);
    float difference = predicted - target;

    // A small class penalty helps preserve distinct five-level regions
    // without turning the whole objective into hard quantisation.
    float levelDifference = (round(predicted * 4.0) - round(target * 4.0)) * 0.25;
    return difference * difference + levelDifference * levelDifference * 0.08;
}

float geneticReduceError(float errorSum)
{
    uint localIndex = gl_LocalInvocationID.x;
    reductionValues[localIndex] = errorSum;
    barrier();
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (localIndex < stride)
            reductionValues[localIndex] += reductionValues[localIndex + stride];
        barrier();
    }
    return reductionValues[0];
}

void scorePopulation()
{
    uint individual = gl_WorkGroupID.x;
    if (individual >= uPopulationSize) return;

    uint localIndex = gl_LocalInvocationID.x;
    uint sampleCount = max(uSampleGrid, 1u) * max(uSampleGrid, 1u);
    uint base = geneticPopulationBase(uSourceIndex, individual);
    float errorSum = 0.0;

    for (uint sampleIndex = localIndex; sampleIndex < sampleCount; sampleIndex += 64u)
    {
        ivec2 pixel = geneticSamplePixel(sampleIndex);
        errorSum += geneticPatternError(pixel, base);
    }

    float totalError = geneticReduceError(errorSum);
    if (localIndex == 0u)
        geneticScores[individual] = totalError / float(sampleCount);
}

// The archive needs one comparable objective over the complete reference.
// Crop offsets, generation and the adjustable training grid must not change it:
// a zero error on one uniform crop does not mean a correct complete pattern.
ivec2 geneticValidationPixel(uint sampleIndex)
{
    uvec2 cell = uvec2(sampleIndex % NEURAL_GENETIC_VALIDATION_GRID,
                       sampleIndex / NEURAL_GENETIC_VALIDATION_GRID);
    ivec2 size = max(uReferenceSize, ivec2(1));
    ivec2 first = ivec2(cell) * size / int(NEURAL_GENETIC_VALIDATION_GRID);
    ivec2 end = max(first + 1,
        ivec2(cell + 1u) * size / int(NEURAL_GENETIC_VALIDATION_GRID));
    uint h0 = geneticHash(sampleIndex ^ (uSeed * 0x85ebca6bu) ^ 0xd1b54a35u);
    uint h1 = geneticHash(h0 ^ 0x68bc21ebu);
    ivec2 jitter = ivec2(h0 % uint(end.x - first.x), h1 % uint(end.y - first.y));
    return min(first + jitter, size - 1);
}

void validatePopulation()
{
    uint validationIndex = gl_WorkGroupID.x;
    if (validationIndex >= 2u) return;
    uint localIndex = gl_LocalInvocationID.x;
    if (localIndex == 0u)
    {
        uint best = 0u;
        for (uint individual = 1u; individual < uPopulationSize; ++individual)
            if (geneticScores[individual] < geneticScores[best]) best = individual;
        sharedBestIndex = best;
    }
    barrier();

    // Individual zero is always the archived champion: reset/resume inserts it,
    // and evolve copies the archive into zero of the destination population.
    uint individual = validationIndex == 0u ? sharedBestIndex : 0u;
    uint base = geneticPopulationBase(uSourceIndex, individual);
    const uint sampleCount = NEURAL_GENETIC_VALIDATION_GRID * NEURAL_GENETIC_VALIDATION_GRID;
    float errorSum = 0.0;
    for (uint sampleIndex = localIndex; sampleIndex < sampleCount; sampleIndex += 64u)
        errorSum += geneticPatternError(geneticValidationPixel(sampleIndex), base);
    float totalError = geneticReduceError(errorSum);
    if (localIndex == 0u)
        geneticScores[uPopulationSize + validationIndex] = totalError / float(sampleCount);
}

uint selectTournamentParent(uint randomSeed)
{
    uint best = geneticHash(randomSeed) % uPopulationSize;
    float bestScore = sharedScores[best];
    for (uint attempt = 1u; attempt < 7u; ++attempt)
    {
        uint candidate = geneticHash(randomSeed + attempt * 0x9e3779b9u)
                       % uPopulationSize;
        float candidateScore = sharedScores[candidate];
        if (candidateScore < bestScore)
        {
            best = candidate;
            bestScore = candidateScore;
        }
    }
    return best;
}

void evolvePopulation()
{
    uint localIndex = gl_LocalInvocationID.x;
    for (uint individual = localIndex; individual < uPopulationSize; individual += 64u)
        sharedScores[individual] = geneticScores[individual];
    barrier();

    if (localIndex == 0u)
    {
        uint best = 0u;
        for (uint individual = 1u; individual < uPopulationSize; ++individual)
            if (sharedScores[individual] < sharedScores[best]) best = individual;
        sharedBestIndex = best;

        float candidateScore = geneticScores[uPopulationSize];
        float incumbentScore = geneticScores[uPopulationSize + 1u];
        geneticBestScore = incumbentScore;
        if (candidateScore < incumbentScore)
        {
            geneticBestScore = candidateScore;
            geneticBestGeneration = uGeneration;
            uint source = geneticPopulationBase(uSourceIndex, best);
            for (uint parameter = 0u;
                 parameter < uint(NEURAL_PATTERN_PARAMETER_COUNT); ++parameter)
                geneticBestWeights[parameter] = neuralPatternWeights[source + parameter];
        }
    }
    memoryBarrierBuffer();
    barrier();

    uint destinationHalf = 1u - uSourceIndex;
    for (uint child = localIndex; child < uPopulationSize; child += 64u)
    {
        uint destination = geneticPopulationBase(destinationHalf, child);
        if (child == 0u)
        {
            // Exact elitism from the best network seen over all generations.
            for (uint parameter = 0u;
                 parameter < uint(NEURAL_PATTERN_PARAMETER_COUNT); ++parameter)
                neuralPatternWeights[destination + parameter] =
                    geneticBestWeights[parameter];
            geneticScores[child] = geneticBestScore;
            continue;
        }
        if (child == 1u)
        {
            // Keep the local crop winner too, even if it is not yet the best
            // complete pattern. Otherwise the archive can erase local progress.
            uint source = geneticPopulationBase(uSourceIndex, sharedBestIndex);
            for (uint parameter = 0u;
                 parameter < uint(NEURAL_PATTERN_PARAMETER_COUNT); ++parameter)
                neuralPatternWeights[destination + parameter] =
                    neuralPatternWeights[source + parameter];
            geneticScores[child] = 1.0e30;
            continue;
        }

        uint childSeed = geneticHash(uSeed ^ (uGeneration * 0x243f6a88u)
                                   ^ (child * 0x85a308d3u));
        uint parentA = selectTournamentParent(childSeed);
        uint parentB = selectTournamentParent(childSeed ^ 0x68bc21ebu);
        uint sourceA = geneticPopulationBase(uSourceIndex, parentA);
        uint sourceB = geneticPopulationBase(uSourceIndex, parentB);
        bool immigrant = (child % 31u) == 30u;

        for (uint parameter = 0u;
             parameter < uint(NEURAL_PATTERN_PARAMETER_COUNT); ++parameter)
        {
            uint geneSeed = geneticHash(childSeed ^ (parameter * 0x9e3779b9u));
            float value;
            if (immigrant)
            {
                value = geneticRandomSigned(geneSeed) * 0.75;
            }
            else
            {
                value = (geneSeed & 1u) == 0u
                    ? neuralPatternWeights[sourceA + parameter]
                    : neuralPatternWeights[sourceB + parameter];
                float mutate = geneticRandom01(geneSeed ^ 0x13198a2eu);
                if (mutate < 0.16)
                {
                    float approximatelyNormal =
                        geneticRandom01(geneSeed ^ 0x03707344u)
                      + geneticRandom01(geneSeed ^ 0xa4093822u)
                      + geneticRandom01(geneSeed ^ 0x299f31d0u) - 1.5;
                    value += approximatelyNormal * uMutationScale;
                }
            }
            neuralPatternWeights[destination + parameter] = clamp(value, -3.0, 3.0);
        }
        geneticScores[child] = 1.0e30;
    }

    if (localIndex == 0u)
    {
        geneticGeneration = uGeneration + 1u;
        geneticSourceIndex = destinationHalf;
    }
}

void main()
{
    if (uPopulationSize == 0u || uPopulationSize > NEURAL_GENETIC_MAX_POPULATION)
        return;
    if (uPass == 0u) initialisePopulation();
    else if (uPass == 1u) scorePopulation();
    else if (uPass == 2u) evolvePopulation();
    else if (uPass == 3u) validatePopulation();
}
