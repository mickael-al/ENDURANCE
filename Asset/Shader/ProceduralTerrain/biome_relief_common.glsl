// Shared 2D primitives; individual biome recipes include only this dimension.
#ifndef ENDURANCE_BIOME_RELIEF_COMMON_GLSL
#define ENDURANCE_BIOME_RELIEF_COMMON_GLSL

#include "field_noise_2d.glsl"

float fieldSurfaceFbm(vec2 p, uint salt)
{
    // Three bounded octaves. Small-scale visual grain belongs in the material.
    return fieldNoise2(p, salt) * 0.57142857
        + fieldNoise2(p * 2.03 + vec2(19.7, -37.1), salt + 1u) * 0.28571429
        + fieldNoise2(p * 4.11 + vec2(-43.2, 11.8), salt + 2u) * 0.14285714;
}

vec2 fieldSurfaceRotate(vec2 p, vec2 direction)
{
    return vec2(dot(p, direction), dot(p, vec2(-direction.y, direction.x)));
}

float fieldSurfaceMound(float distanceToSite, float radius)
{
    return 1.0 - smoothstep(0.0, radius, distanceToSite);
}

float fieldSurfaceMesa(float distanceToSite, float radius, float crown)
{
    return 1.0 - smoothstep(radius * crown, radius, distanceToSite);
}

float fieldSurfaceLandmark(vec2 p, float spacing, uint salt, float metric,
                           float radius, float crown, float occupancy)
{
    vec2 site = fieldCellular2Data(p / spacing, salt, metric);
    // Site metadata costs no extra noise sample. A disabled site leaves an
    // uninterrupted floor instead of a small obstacle. Compact support ends
    // before the nearest-site boundary, so changing site IDs cannot make a seam.
    float enabled = step(1.0 - occupancy, site.y);
    float strength = 0.65 + 0.35 * clamp((site.y - (1.0 - occupancy)) / occupancy, 0.0, 1.0);
    // Different footprints and crowns keep rows of sites from becoming rows
    // of identical cylinders. The contour cannot expand beyond the original
    // radius: 0.96*r + 0.04*r = r, preserving its spacing bound and continuity.
    float radiusVariation = 0.72 + 0.24 * fract(site.y * 23.17);
    float crownVariation = clamp(crown * (0.65 + 0.65 * fract(site.y * 41.73)), 0.0, 0.85);
    float contour = fieldNoise2(p * (3.0 / spacing), salt ^ 0xa511e9b3u) * (0.04 * radius);
    return fieldSurfaceMesa(site.x + contour, radius * radiusVariation, crownVariation) * enabled * strength;
}

float fieldSurfaceTerrace(float height, float stepHeight, float softness)
{
    // Step size is expressed directly in metres, including negative heights.
    // The broad transition region keeps a usable ramp between level shelves.
    float cell = height / stepHeight;
    float halfWidth = softness * 0.5;
    return (floor(cell) + smoothstep(0.5 - halfWidth, 0.5 + halfWidth, fract(cell))) * stepHeight;
}

float fieldSurfaceDune(vec2 p, vec2 direction, float wavelength, uint salt)
{
    vec2 oriented = fieldSurfaceRotate(p, direction);
    // A slow phase warp bends a dune train without scattering unrelated bumps.
    float warp = fieldNoise2(p * (1.0 / (wavelength * 3.0)), salt) * 0.32;
    float phase = fract(oriented.x / wavelength + warp);
    // Long windward ramp, shorter lee slope, C1 continuity at each join.
    return phase < 0.76 ? smoothstep(0.0, 0.76, phase)
                        : 1.0 - smoothstep(0.76, 1.0, phase);
}

vec4 fieldClampRelief(vec4 relief)
{
    return clamp(relief, vec4(-640.0, 0.0, -160.0, 0.0),
                         vec4(640.0, 576.0, 160.0, 1.0));
}

#endif
