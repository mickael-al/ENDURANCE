#ifndef ENDURANCE_STEPPE_CAVES_GLSL
#define ENDURANCE_STEPPE_CAVES_GLSL
#include "regions.glsl"

// One candidate per 36 km2, accepted with probability .10 before the mountain
// gates: nominally one candidate per 360 km2, not one cave every 6000 m.
const float STEPPE_CAVE_SPACING = 6000.0;
const float STEPPE_CAVE_OCCUPANCY = 0.10;
const uint STEPPE_CAVE_SALT = 330u;
const float STEPPE_CAVE_BAND = 4096.0;
const float STEPPE_CAVE_PLAIN_CEILING = 60.0;
const float STEPPE_CAVE_BEDROCK = 30.0;

LandSite steppeCaveSite(vec2 p)
{
    return landSite(p, STEPPE_CAVE_SPACING, STEPPE_CAVE_SALT, STEPPE_CAVE_OCCUPANCY);
}

// Local +X direction, useful for placing/diagnosing the explicit entrance.
// This reproduces landSite's deterministic orientation at the same world cell.
vec2 steppeCaveAxis(LandSite site)
{
    ivec2 cell = ivec2(floor(site.center / STEPPE_CAVE_SPACING));
    uint h = noiseInternalHash(uint(cell.x) * 0x9e3779b9u
        ^ uint(cell.y) * 0x85ebca6bu ^ uSeed ^ STEPPE_CAVE_SALT);
    vec2 direction = vec2(noiseInternalUintTo01(noiseInternalHash(h ^ 0x7f4a7c15u)),
        noiseInternalUintTo01(noiseInternalHash(h ^ 0x94d049bbu))) * 2.0 - 1.0;
    return dot(direction, direction) > 0.0001 ? normalize(direction) : vec2(1.0, 0.0);
}

float steppeCaveSupport(vec2 p, LandSite site, float presence)
{
    if (site.mask <= 0.0) return 0.0;
    float centerPresence = steppeMountainPresence(site.center);
    return site.mask * smoothstep(0.90, 0.98, centerPresence)
        * smoothstep(0.75, 0.95, presence);
}

float steppeCaveHeightGate(float mountainMetres)
{
    return smoothstep(360.0, 600.0, mountainMetres);
}

float steppeCaveGalleryHeightGate(float mountainMetres)
{
    // The rising gallery needs much less cover than the large buried rooms.
    // Keeping the chamber's height gate here could seal a valid side entrance.
    return smoothstep(120.0, 200.0, mountainMetres);
}

float steppeCaveSegmentDistance(vec2 p, vec2 a, vec2 b)
{
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    return length(p - mix(a, b, t));
}

float steppeCaveGalleryFootprint(LandSite site)
{
    vec2 q = site.q;
    float distance = min(steppeCaveSegmentDistance(q, vec2(-100.0, -20.0), vec2(105.0, -55.0)),
        min(steppeCaveSegmentDistance(q, vec2(105.0, -55.0), vec2(390.0, 65.0)),
            steppeCaveSegmentDistance(q, vec2(390.0, 65.0), vec2(650.0, 120.0))));
    distance = min(distance, steppeCaveSegmentDistance(q, vec2(-100.0, -20.0), vec2(165.0, 80.0)));
    // Includes maximum tunnel radius, broad deformation and smoothing margin.
    return 1.0 - smoothstep(70.0, 100.0, distance);
}

float steppeCaveBand(LandSite site, float support, float mountainMetres)
{
    // Full band before the effect reaches appreciable strength; landDelta then
    // fades safely inside its edges. Geometry is well inside the 716.8 m half
    // band at reference detail=.35 (deepest chamber floor <400 m).
    float eligible = support * max(steppeCaveHeightGate(mountainMetres),
        steppeCaveGalleryHeightGate(mountainMetres) * steppeCaveGalleryFootprint(site));
    return STEPPE_CAVE_BAND * smoothstep(0.0, 0.08, eligible);
}

float steppeCaveDepth(LandSite site, float availableMetres)
{
    return min(mix(190.0, 270.0, site.variant), max(availableMetres, 0.0) * 0.45);
}

vec3 steppeCaveChamberCenter(LandSite site, float depth)
{
    return vec3(-100.0, -depth, -20.0);
}

vec3 steppeCaveChamberRadii(LandSite site)
{
    return mix(vec3(190.0, 60.0, 120.0), vec3(290.0, 85.0, 180.0), site.variant);
}

vec3 steppeCaveEntrance()
{
    return vec3(650.0, 55.0, 120.0);
}
#endif
