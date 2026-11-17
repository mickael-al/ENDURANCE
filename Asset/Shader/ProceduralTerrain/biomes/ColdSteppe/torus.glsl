#ifndef ENDURANCE_STEPPE_TORUS_GLSL
#define ENDURANCE_STEPPE_TORUS_GLSL
#include "regions.glsl"
#include "torus_settings.glsl"
#include "torus_porosity.glsl"

// Each complete ring fits inside its own cell: 2500 m minimum centre-to-edge
// distance, versus 1050 m outer radius plus 128 m of dispatch support.
// Consequently no neighbouring-cell search is needed, including at negative
// coordinates (floor, never truncation, defines the same cell on both passes).
struct SteppeTorusSite
{
    vec2 center;
    uint key;
};

SteppeTorusSite steppeTorusInCell(ivec2 cell)
{
    uint key = noiseInternalHash(uint(cell.x) * 0x9e3779b9u
        ^ uint(cell.y) * 0x85ebca6bu ^ uSeed ^ 0x6c8e9cf5u);
    vec2 jitter = vec2(noiseInternalUintTo01(noiseInternalHash(key ^ 0x7f4a7c15u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x94d049bbu)));
    return SteppeTorusSite((vec2(cell) + 0.25 + 0.5 * jitter)
        * STEPPE_TORUS_SPACING, key);
}

SteppeTorusSite steppeTorusSite(vec2 p)
{
    return steppeTorusInCell(ivec2(floor(p / STEPPE_TORUS_SPACING)));
}

float steppeTorusFootprint(vec2 p, SteppeTorusSite site)
{
    return abs(length(p - site.center) - STEPPE_TORUS_RADIUS)
        - STEPPE_TORUS_TUBE_RADIUS;
}

float steppeTorusEligibility(SteppeTorusSite site)
{
    // Accept/reject a WHOLE site, rather than multiplying its individual arcs
    // by the mountain mask. Sample a padded disk perimeter as well as its
    // centre; this is conservative for the broad 38-km regional field.
    // The test suite also probes the entire accepted annulus between samples.
    if (steppeMountainPresence(site.center) > 0.0) return 0.0;
    float reach = STEPPE_TORUS_RADIUS + STEPPE_TORUS_TUBE_RADIUS + 250.0;
    for (int i = 0; i < 16; ++i)
    {
        float angle = float(i) * (6.28318530718 / 16.0);
        vec2 p = site.center + reach * vec2(cos(angle), sin(angle));
        if (steppeMountainPresence(p) > 0.0) return 0.0;
    }
    return 1.0;
}

// Evaluated only in the surface pass. Preserve the expensive mountain/cave
// functions, but use ColdSteppe's otherwise unused pilot to cache torus support.
// A mixed biome column cannot decode this pilot independently; its volume pass
// instead recomputes site eligibility, only near a candidate's annulus.
float steppeTorusControl(vec2 p)
{
    if (uFieldMode != 0u || uDetailScale <= 0.0) return 0.0;
    SteppeTorusSite site = steppeTorusSite(p);
    if (steppeTorusFootprint(p, site) >= 128.0) return 0.0;
    return steppeTorusEligibility(site);
}

float steppeTorusCenterHeight()
{
    return STEPPE_TORUS_TUBE_RADIUS * (1.0 - 2.0 * STEPPE_TORUS_BURIED_FRACTION);
}

vec3 steppeTorusLocal(vec3 p, SteppeTorusSite site, float ground)
{
    // Terrain-relative Y preserves 25 m burial around the complete ring.
    // The plan view is circular; the ring follows the plain's gentle relief
    // vertically, rather than floating where a rigid horizontal torus crosses
    // a depression. Both radial and vertical tube radii remain 50 world metres.
    return vec3(p.x - site.center.x, p.y - ground - steppeTorusCenterHeight(),
        p.z - site.center.y);
}

// Standard torus SDF, negative inside; Y is the axis through its open centre.
float steppeTorusEnvelope(vec3 q)
{
    return length(vec2(length(q.xz) - STEPPE_TORUS_RADIUS, q.y))
        - STEPPE_TORUS_TUBE_RADIUS;
}

float steppeTorusGeometry(vec3 p, SteppeTorusSite site, float ground)
{
    vec3 q = steppeTorusLocal(p, site, ground);
    float envelope = steppeTorusEnvelope(q);
    if (envelope >= 64.0) return -envelope;
    // Intersection, never a displacement: pores remove only the rock within
    // the requested torus, leaving the large central opening entirely empty.
    return min(-envelope, steppeTorusPoreField(q, site.key) * 100.0);
}

float steppeTorusBand(vec2 p, float control)
{
    if (control <= 0.0 || uDetailScale <= 0.0) return 0.0;
    float offset = uBandOffset;
    if (uBandOffsetVariation > 0.0)
        offset += uBandOffsetVariation * noisePerlin2D(p * 0.0004, uSeed ^ 0xa4093822u);
    // Include the full geometry fade (64 m) plus storage/roundoff margin.
    float halfBand = abs(offset) + abs(steppeTorusCenterHeight())
        + STEPPE_TORUS_TUBE_RADIUS + 80.0;
    float edge = steppeTorusFootprint(p, steppeTorusSite(p));
    float dispatch = 1.0 - smoothstep(80.0, 128.0, edge);
    return min(2.0 * halfBand / uDetailScale, FIELD_MAX_VOLUME_THICKNESS) * dispatch;
}

#endif
