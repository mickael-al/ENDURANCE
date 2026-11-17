#ifndef ENDURANCE_COLD_MARSH_POROSITY_GLSL
#define ENDURANCE_COLD_MARSH_POROSITY_GLSL
#include "../../../Noise/noise_3d.glsl"
#include "sphere_settings.glsl"

vec3 coldMarshNoisePhase(uint key)
{
    return vec3(
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x93ad2167u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x51f2e183u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x68bc21ebu))) * 256.0;
}

vec3 coldMarshVoronoiCoordinates(vec3 p, uint key, vec3 phase)
{
    // Three independent, fully volumetric Perlins bend the large cell walls.
    // Keep the original coordinate term: pure noise coordinates would fold all
    // space into a bounded sample and repeat the same few cavities everywhere.
    vec3 warp = noiseVector3D(p / COLD_MARSH_WARP_SCALE + phase.yzx,
        key ^ 0x91ca723du);
    return p / COLD_MARSH_VORONOI_SCALE + phase
        + COLD_MARSH_WARP_STRENGTH * warp;
}

// Positive stone, negative air, in distance-like world-metre units. The caller
// intersects this field with the sphere and unions the original ground back in.
float coldMarshPorousField(vec3 p, uint key)
{
    vec3 phase = coldMarshNoisePhase(key);
    vec3 domain = coldMarshVoronoiCoordinates(p, key, phase);
    float large = (noiseVoronoi3D01(domain, key ^ 0x39d7af25u)
        - COLD_MARSH_PORE_THRESHOLD) * COLD_MARSH_VORONOI_SCALE;
    // A single smaller Perlin roughens the broad curves without replacing
    // their overall shape with fine, uniformly distributed holes.
    float detail = noisePerlin3D(p / COLD_MARSH_DETAIL_SCALE + phase,
        key ^ 0xb74319dbu) * COLD_MARSH_DETAIL_AMPLITUDE;
    return large + detail;
}

#endif
