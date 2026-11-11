#version 450 core

// The genetic trainer writes the current best scalar pattern here. The preview
// reads it directly, so no generated GLSL or shader recompilation is needed.
layout(std430, binding = 5) readonly buffer NeuralBestBuffer
{
    float neuralPatternWeights[];
};

#include "../Noise/noise_regions_2d.glsl"
#include "../Noise/neural_pattern_2d.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

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
uniform float uHumidityFrequencyMultiplier;
uniform float uTemperatureFrequencyMultiplier;
uniform int uHumidityNoiseType;
uniform int uTemperatureNoiseType;
uniform int uOctaves;
uniform float uLacunarity;
uniform float uGain;
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

float sampleClimateAxis(int type, vec2 world, float frequencyMultiplier, uint seed)
{
    if (type == NOISE_NEURAL_PATTERN)
    {
        // Origin zero + extent equal to the period maps the complete reference
        // image to [0,1]^2. The same trained scalar pattern can independently
        // drive humidity or temperature through their two noise selectors.
        vec2 p = world / max(uNeuralWorldPeriod, 1.0);
        p = p * max(frequencyMultiplier, 0.0) + vec2(0.5);
        return noiseNeuralPattern2D01(p, uSeed, 0u);
    }

    vec2 p = world * uClimateFrequency * max(frequencyMultiplier, 0.0);
    return climateTo01(sampleNoise2D(type, p, seed));
}

vec2 sampleClimate(vec2 world)
{
    float humidity = sampleClimateAxis(uHumidityNoiseType, world,
        uHumidityFrequencyMultiplier, uSeed ^ 0x243f6a88u);
    float temperature = sampleClimateAxis(uTemperatureNoiseType, world,
        uTemperatureFrequencyMultiplier, uSeed ^ 0x85a308d3u);
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
    float target = round(value * 4.0) * 0.25;
    return clamp(value + clamp(target - value, -maxShift, maxShift), 0.0, 1.0);
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

vec3 matrixColor(vec2 climate)
{
    climate = clamp(climate, 0.0, 1.0);
    if (!uMatrixBlend)
    {
        ivec2 node = ivec2(round(climate * 4.0));
        int id = uBiomeMatrix[node.x + node.y * 5];
        return BIOME_COLORS[clamp(id, 0, 25)];
    }

    vec2 grid = climate * 4.0;
    ivec2 first = min(ivec2(floor(grid)), ivec2(3));
    ivec2 second = first + ivec2(1);
    vec2 f = quintic(grid - vec2(first));
    int id00 = uBiomeMatrix[first.x  + first.y  * 5];
    int id10 = uBiomeMatrix[second.x + first.y  * 5];
    int id01 = uBiomeMatrix[first.x  + second.y * 5];
    int id11 = uBiomeMatrix[second.x + second.y * 5];
    vec3 row0 = mix(BIOME_COLORS[clamp(id00,0,25)], BIOME_COLORS[clamp(id10,0,25)], f.x);
    vec3 row1 = mix(BIOME_COLORS[clamp(id01,0,25)], BIOME_COLORS[clamp(id11,0,25)], f.x);
    return mix(row0, row1, f.y);
}

void main()
{
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(pixel, uvec2(uResolution)))) return;

    vec2 uv = (vec2(pixel) + 0.5) / float(uResolution);
    vec2 world = uWorldOrigin + (uv - 0.5) * uWorldExtent;
    vec2 climate = applyPostShape(sampleClimate(world));
    vec2 attracted = applyAttraction(climate);

    ivec2 destination = ivec2(pixel);
    imageStore(uTemperatureImage, destination, vec4(vec3(climate.y), 1.0));
    imageStore(uHumidityImage, destination, vec4(vec3(climate.x), 1.0));
    imageStore(uCombinedImage, destination, vec4(climate.y, climate.x, 0.0, 1.0));
    imageStore(uAttractedImage, destination, vec4(attracted.y, attracted.x, 0.0, 1.0));
    imageStore(uMatrixImage, destination, vec4(matrixColor(attracted), 1.0));
}
