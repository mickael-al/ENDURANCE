#ifndef ENDURANCE_BOREAL_FOREST_HEIGHT_GLSL
#define ENDURANCE_BOREAL_FOREST_HEIGHT_GLSL
#include "../../biome_landforms_2d.glsl"
#include "settings.glsl"

float borealGroundHeight(vec2 p)
{
    // Original global ground, including fine detail; only the old dome removed.
    vec2 q=landWarp(p,2800.0,210.0,701u);
    float rounded=.5+.5*landFbm(q*vec2(.00065,.00039),702u);
    float regional=.5+.5*noiseSimplex2D(q*.00019,uSeed^703u);
    float h=rounded*rounded*(210.0+110.0*regional)-72.0;
    h+=landFbm(q*vec2(.0015,.00086),704u)*24.0;
    h+=noiseSimplex2D(q*.008,uSeed^706u)*1.4;
    return h;
}

vec4 borealLayerNoise(vec2 p)
{
    vec2 q = p / BOREAL_MOUNTAIN_SCALE;
    return vec4(
        noiseSimplexFbm2D01(q, uSeed ^ 710u, BOREAL_MOUNTAIN_OCTAVES, 2.0, 0.5),
        noiseSimplexFbm2D01(q, uSeed ^ 711u, BOREAL_MOUNTAIN_OCTAVES, 2.0, 0.5),
        noiseSimplexFbm2D01(q, uSeed ^ 712u, BOREAL_MOUNTAIN_OCTAVES, 2.0, 0.5),
        noiseSimplexFbm2D01(q, uSeed ^ 713u, BOREAL_MOUNTAIN_OCTAVES, 2.0, 0.5));
}

float borealLayerCount(vec2 p)
{
    // Intentionally discrete and STRICTLY greater than .7, as requested.
    return dot(vec4(greaterThan(borealLayerNoise(p),
        vec4(BOREAL_MOUNTAIN_THRESHOLD))), vec4(1.0));
}

float borealMountainHeight(vec2 p)
{
    return borealLayerCount(p) * BOREAL_LAYER_HEIGHT;
}

float borealMountainFromControl(float control)
{
    // Four possible increments survive the packed 16-bit control exactly.
    return round(clamp(control, 0.0, 1.0) * 4.0) * BOREAL_LAYER_HEIGHT;
}

float borealMountainBand(vec2 p, float mountain)
{
    if (mountain <= 0.0 || uFieldMode != 0u || uDetailScale <= 0.0) return 0.0;
    float offset = uBandOffset;
    if (uBandOffsetVariation > 0.0)
        offset += uBandOffsetVariation * noisePerlin2D(p * 0.0004, uSeed ^ 0xa4093822u);
    // Cover the full added stack to the preserved ground, with fade margins.
    return min(2.0 * (mountain + (abs(offset) + 80.0) / uDetailScale),
        FIELD_MAX_VOLUME_THICKNESS);
}

#endif
