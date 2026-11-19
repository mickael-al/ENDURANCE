// Statistics for the displayed climate matrix only. No extra noise evaluation
// or image readback: reuse the exact IDs/weights that colour each preview pixel.
#ifndef ENDURANCE_CLIMATE_MATRIX_STATISTICS_GLSL
#define ENDURANCE_CLIMATE_MATRIX_STATISTICS_GLSL

const uint CLIMATE_STATS_BIOMES = 26u;
const float CLIMATE_STATS_WEIGHT_SCALE = 16777216.0; // Q24, not 8-bit image colour
layout(std430, binding = 6) buffer ClimateMatrixStatistics
{
    // Weighted surface as low/high uint32 words, dominant pixels, present pixels.
    uvec4 climateMatrixTotals[26];
};
shared uint climateGroupWeights[26];
shared uint climateGroupDominant[26];
shared uint climateGroupPresent[26];

void climateMatrixStatsBegin()
{
    uint i = gl_LocalInvocationIndex;
    if (i < CLIMATE_STATS_BIOMES)
    {
        climateGroupWeights[i] = 0u;
        climateGroupDominant[i] = 0u;
        climateGroupPresent[i] = 0u;
    }
    barrier();
}

void climateMatrixStatsRecord(ivec4 ids, vec4 weights)
{
    // A future matrix may repeat an ID: count that biome only once per pixel,
    // and compare its combined weight when choosing the dominant biome.
    for (int i = 1; i < 4; ++i)
        for (int j = 0; j < i; ++j)
            if (ids[i] == ids[j])
            {
                weights[j] += weights[i];
                weights[i] = 0.0;
                break;
            }

    int dominant = -1;
    float largest = -1.0;
    for (int i = 0; i < 4; ++i)
    {
        float weight = clamp(weights[i], 0.0, 1.0);
        if (weight <= 0.0) continue;
        uint id = uint(ids[i]);
        atomicAdd(climateGroupWeights[id], uint(round(weight * CLIMATE_STATS_WEIGHT_SCALE)));
        // Presence stays exact even when a tiny weight rounds to zero in Q24.
        atomicAdd(climateGroupPresent[id], 1u);
        if (weight > largest || (weight == largest && ids[i] < dominant))
        {
            largest = weight;
            dominant = ids[i];
        }
    }
    if (dominant >= 0) atomicAdd(climateGroupDominant[dominant], 1u);
}

void climateMatrixStatsEnd()
{
    barrier();
    uint i = gl_LocalInvocationIndex;
    if (i >= CLIMATE_STATS_BIOMES || climateGroupPresent[i] == 0u) return;

    // 64 pixels * 2^24 fits in a shared uint32. The full image needs two words;
    // carry propagation uses core uint atomics, without an int64 extension.
    uint amount = climateGroupWeights[i];
    if (amount != 0u)
    {
        uint previous = atomicAdd(climateMatrixTotals[i].x, amount);
        if (previous > 0xffffffffu - amount) atomicAdd(climateMatrixTotals[i].y, 1u);
    }
    atomicAdd(climateMatrixTotals[i].z, climateGroupDominant[i]);
    atomicAdd(climateMatrixTotals[i].w, climateGroupPresent[i]);
}

#endif
