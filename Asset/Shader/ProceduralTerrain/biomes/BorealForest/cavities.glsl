#ifndef ENDURANCE_BOREAL_CAVITIES_GLSL
#define ENDURANCE_BOREAL_CAVITIES_GLSL
#include "../../../Noise/noise_2d.glsl"
#include "../../../Noise/noise_3d.glsl"
#include "settings.glsl"

// Calibrated 60th percentile of the COMPOSED field below, not a claim that
// Voronoi's normalized distances have a uniform distribution. Around 60% of
// the unbounded volume is cavity; a particular finite mountain may differ.
// Reproduce/calibrate with Tests/Terrain/calibrate_boreal_cavities.py.
const float BOREAL_CAVITY_THRESHOLD = 0.472800523;

vec3 borealCavityCoordinates(vec3 p, uint seed)
{
    vec3 q = p / BOREAL_CAVITY_WARP_SCALE;
    // Three independently seeded 2D Perlins are injected into the 3D domain.
    // Y participates in two planes: this is a volumetric cavity field, not an
    // XZ mask extruded vertically. Keeping p/scale retains the world extent.
    vec3 warp = vec3(
        noisePerlin2D(q.yz, seed ^ 0xb74319dbu),
        noisePerlin2D(q.zx, seed ^ 0x5d2e8f61u),
        noisePerlin2D(q.xy, seed ^ 0x91ca723du));
    return p / BOREAL_CAVITY_SCALE + BOREAL_CAVITY_WARP_STRENGTH * warp;
}

float borealCavityNoise(vec3 p, uint seed)
{
    return noiseVoronoi3D01(borealCavityCoordinates(p, seed),
        seed ^ 0x39d7af25u);
}

// Positive stone, negative air. Distance-like units in world metres; the
// domain warp means this is not an exact signed distance for sphere tracing.
// The caller intersects excavation with the added mountain slab, protecting
// the unchanged soil below it. This helper has no terrain/biome dependency.
float borealCavityField(vec3 p, uint seed)
{
    return (borealCavityNoise(p, seed) - BOREAL_CAVITY_THRESHOLD)
        * BOREAL_CAVITY_SCALE;
}

#endif
