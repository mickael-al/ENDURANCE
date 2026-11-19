// Broad 3D Perlin pores; the signed field is positive inside surviving stone.
#ifndef ENDURANCE_STEPPE_TORUS_POROSITY_GLSL
#define ENDURANCE_STEPPE_TORUS_POROSITY_GLSL
#include "../../../Noise/noise_3d.glsl"
#include "torus_settings.glsl"

// Quantiles of the actual signed Perlin implementation, at probabilities
// .30,.35,...,.80. Offline calibration samples seeds, lattice cells and phases;
// a noise value is not itself a uniform probability. Local finite rings can
// differ from these ensemble proportions, especially with intentionally large
// pores. Linear interpolation keeps the density continuous along Y.
const float STEPPE_TORUS_PORE_QUANTILES[11] = float[11](
    -0.151485468, -0.110844376, -0.071881191, -0.034941806,
    0.000101832, 0.035175759, 0.072041690, 0.111013411,
    0.151942262, 0.195012942, 0.240835688);

float steppeTorusSolidFraction(float localY)
{
    // The complete 100-m tube, including its buried quarter, spans -50..50.
    return mix(0.30, 0.80, clamp(localY / (2.0 * STEPPE_TORUS_TUBE_RADIUS)
        + 0.5, 0.0, 1.0));
}

float steppeTorusPoreThreshold(float fraction)
{
    float index = clamp((fraction - 0.30) * 20.0, 0.0, 10.0);
    int first = min(int(floor(index)), 9);
    return mix(STEPPE_TORUS_PORE_QUANTILES[first],
        STEPPE_TORUS_PORE_QUANTILES[first + 1], index - float(first));
}

float steppeTorusPoreNoise(vec3 q, uint key)
{
    vec3 phase = vec3(
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x93ad2167u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x51f2e183u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x68bc21ebu))) * 256.0;
    return noisePerlin3D(q / STEPPE_TORUS_PORE_SCALE + phase,
        noiseInternalHash(key ^ 0x641ac379u));
}

float steppeTorusPoreField(vec3 q, uint key)
{
    return steppeTorusPoreThreshold(steppeTorusSolidFraction(q.y))
        - steppeTorusPoreNoise(q, key);
}
#endif
