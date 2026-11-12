#version 450 core

// The genetic trainer writes the current best scalar pattern here. The preview
// reads it directly, so no generated GLSL or shader recompilation is needed.
layout(std430, binding = 5) readonly buffer NeuralBestBuffer
{
    float neuralPatternWeights[];
};

#include "../Noise/noise_regions_2d.glsl"
#include "../Noise/neural_pattern_2d.glsl"
#include "climate_experiments.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "matrix_statistics.glsl"

layout(rgba8, binding = 0) uniform writeonly image2D uTemperatureImage;
layout(rgba8, binding = 1) uniform writeonly image2D uHumidityImage;
layout(rgba8, binding = 2) uniform writeonly image2D uCombinedImage;
layout(rgba8, binding = 3) uniform writeonly image2D uAttractedImage;
layout(rgba8, binding = 4) uniform writeonly image2D uMatrixImage;

uniform uint uResolution;
uniform uint uSeed;
uniform vec2 uWorldOrigin;
uniform float uWorldExtent;
uniform float uClimateFrequency;
uniform float uNeuralWorldPeriod;
uniform bool uNeuralReferencePreview;
uniform float uHumidityFrequencyMultiplier;
uniform float uTemperatureFrequencyMultiplier;
uniform int uHumidityNoiseType;
uniform int uTemperatureNoiseType;
uniform int uOctaves;
uniform float uLacunarity;
uniform float uGain;
uniform vec2 uBinaryTransition; // humidity, temperature; noise-value half-widths
uniform float uRemapMinimum;
uniform float uRemapMaximum;
uniform vec2 uPostContrast;
uniform vec2 uPostGamma;
uniform int uAttractionMode;
uniform vec2 uAttractionTarget;
uniform float uAttractionMaxShift;
uniform bool uMatrixBlend;
uniform int uBiomeMatrix[25];

const int NOISE_NEURAL_PATTERN = 11;
const int NOISE_BINARY_PERLIN_5 = 12;
const int NOISE_BINARY_SIMPLEX_FBM_5 = 13;

const vec3 BIOME_COLORS[26] = vec3[26](
    vec3(.73,.83,.86), vec3(.53,.59,.57), vec3(.36,.42,.29), vec3(.19,.29,.23), vec3(.17,.25,.21),
    vec3(.39,.42,.39), vec3(.47,.43,.30), vec3(.23,.32,.23), vec3(.21,.30,.27), vec3(.22,.29,.24),
    vec3(.58,.49,.31), vec3(.39,.45,.23), vec3(.27,.37,.20), vec3(.20,.33,.22), vec3(.29,.35,.22),
    vec3(.55,.43,.28), vec3(.58,.50,.25), vec3(.43,.44,.22), vec3(.37,.39,.19), vec3(.18,.33,.18),
    vec3(.14,.15,.16), vec3(.63,.28,.16), vec3(.64,.53,.35), vec3(.26,.27,.16), vec3(.30,.34,.23),
    vec3(.29,.35,.34)
);

float sampleNoise2D(int type, vec2 p, uint seed)
{
    if (type == 1) return noiseSimplex2D(p, seed);
    if (type == 2) return noiseValue2D(p, seed);
    if (type == 3) return noiseValueCubic2D(p, seed);
    if (type == 4) return noiseWhite2D(p, seed);
    if (type == 5) return noiseVoronoi2D(p, seed);
    if (type == 6) return noiseCellValue2D(p, seed);
    if (type == 7) return noiseFbm2D(p, seed, uOctaves, uLacunarity, uGain);
    if (type == 8) return noiseSimplexFbm2D(p, seed, uOctaves, uLacunarity, uGain);
    if (type == 9) return noiseValueFbm2D(p, seed, uOctaves, uLacunarity, uGain);
    if (type == 10) return noisePatchwork2D(p, seed);
    return noisePerlin2D(p, seed);
}

float climateTo01(float value)
{
    float lower = min(uRemapMinimum, uRemapMaximum);
    float upper = max(uRemapMinimum, uRemapMaximum);
    upper = max(upper, lower + 0.000001);
    return smoothstep(lower, upper, value);
}

float sampleClimateAxis(int type, vec2 world, float frequencyMultiplier, uint seed,
                        float binaryTransition)
{
    if (type == NOISE_NEURAL_PATTERN)
    {
        // Preserve the old world scale at the default climate frequency. Both
        // the common frequency and the per-axis multiplier now scale the MLP.
        float frequencyScale = max(uClimateFrequency, 0.0) / 0.0005;
        vec2 p = world / max(uNeuralWorldPeriod, 1.0);
        p = p * (frequencyScale * max(frequencyMultiplier, 0.0)) + vec2(0.5);
        if (uNeuralReferencePreview)
            return noiseNeuralPattern2D01(p, uSeed, 0u);

        // Use the axis seed, including for the periodic Fourier features. Just
        // changing Perlin's seed would leave Fourier-only learned models equal.
        vec2 phase = vec2(noiseInternalUintTo01(noiseInternalHash(seed)),
                          noiseInternalUintTo01(noiseInternalHash(seed ^ 0x68bc21ebu)));
        return noiseNeuralPattern2D01(p + phase, seed, 0u);
    }

    vec2 p = world * uClimateFrequency * max(frequencyMultiplier, 0.0);
    // Already in [0,1]: smooth individual thresholds, not the resulting sum.
    if (type == NOISE_BINARY_PERLIN_5 || type == NOISE_BINARY_SIMPLEX_FBM_5)
        return climateLabSum5(p, seed, type == NOISE_BINARY_SIMPLEX_FBM_5,
                              uOctaves, uLacunarity, uGain, binaryTransition);
    return climateTo01(sampleNoise2D(type, p, seed));
}

vec2 sampleClimate(vec2 world)
{
    float humidity = sampleClimateAxis(uHumidityNoiseType, world,
        uHumidityFrequencyMultiplier, uSeed ^ 0x243f6a88u, uBinaryTransition.x);
    float temperature = sampleClimateAxis(uTemperatureNoiseType, world,
        uTemperatureFrequencyMultiplier, uSeed ^ 0x85a308d3u, uBinaryTransition.y);
    return vec2(humidity, temperature);
}

vec2 applyPostShape(vec2 climate)
{
    climate.x = noiseContrast01(climate.x, max(uPostContrast.x, 0.0));
    climate.y = noiseContrast01(climate.y, max(uPostContrast.y, 0.0));
    climate.x = noiseGamma01(climate.x, max(uPostGamma.x, 0.0001));
    climate.y = noiseGamma01(climate.y, max(uPostGamma.y, 0.0001));
    return clamp(climate, 0.0, 1.0);
}

vec2 applyAttraction(vec2 climate)
{
    if (uAttractionMode == 1)
        return vec2(climateLabAttractNearFifths01(climate.x, uAttractionTarget.x, uAttractionMaxShift),
                    climateLabAttractNearFifths01(climate.y, uAttractionTarget.y, uAttractionMaxShift));
    if (uAttractionMode == 2)
        return vec2(attractNearestMatrixLevel01(climate.x, uAttractionMaxShift),
                    attractNearestMatrixLevel01(climate.y, uAttractionMaxShift));
    return climate;
}

vec2 quintic(vec2 value)
{
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

void matrixSample(vec2 climate, out ivec4 ids, out vec4 weights)
{
    climate = clamp(climate, 0.0, 1.0);
    if (!uMatrixBlend)
    {
        ivec2 node = ivec2(round(climate * 4.0));
        ids = ivec4(clamp(uBiomeMatrix[node.x + node.y * 5], 0, 25));
        weights = vec4(1, 0, 0, 0);
        return;
    }

    vec2 grid = climate * 4.0;
    ivec2 first = min(ivec2(floor(grid)), ivec2(3));
    ivec2 second = first + ivec2(1);
    // Roundoff near 1 must not create negative contributions in the matrix.
    vec2 f = clamp(quintic(grid - vec2(first)), vec2(0.0), vec2(1.0));
    ids = clamp(ivec4(uBiomeMatrix[first.x  + first.y  * 5],
                      uBiomeMatrix[second.x + first.y  * 5],
                      uBiomeMatrix[first.x  + second.y * 5],
                      uBiomeMatrix[second.x + second.y * 5]), ivec4(0), ivec4(25));
    weights = vec4((1.0-f.x)*(1.0-f.y), f.x*(1.0-f.y),
                   (1.0-f.x)*f.y, f.x*f.y);
}

void main()
{
    climateMatrixStatsBegin();
    uvec2 pixel = gl_GlobalInvocationID.xy;
    // Edge invocations must still reach both statistics barriers.
    if (all(lessThan(pixel, uvec2(uResolution))))
    {
        vec2 uv = (vec2(pixel) + 0.5) / float(uResolution);
        vec2 world = uWorldOrigin + (uv - 0.5) * uWorldExtent;
        vec2 climate = applyPostShape(sampleClimate(world));
        vec2 attracted = applyAttraction(climate);
        ivec4 ids;
        vec4 weights;
        matrixSample(attracted, ids, weights);
        vec3 colour = BIOME_COLORS[ids.x]*weights.x + BIOME_COLORS[ids.y]*weights.y
                    + BIOME_COLORS[ids.z]*weights.z + BIOME_COLORS[ids.w]*weights.w;
        climateMatrixStatsRecord(ids, weights);

        ivec2 destination = ivec2(pixel);
        imageStore(uTemperatureImage, destination, vec4(vec3(climate.y), 1.0));
        imageStore(uHumidityImage, destination, vec4(vec3(climate.x), 1.0));
        imageStore(uCombinedImage, destination, vec4(climate.y, climate.x, 0.0, 1.0));
        imageStore(uAttractedImage, destination, vec4(attracted.y, attracted.x, 0.0, 1.0));
        imageStore(uMatrixImage, destination, vec4(colour, 1.0));
    }
    climateMatrixStatsEnd();
}
