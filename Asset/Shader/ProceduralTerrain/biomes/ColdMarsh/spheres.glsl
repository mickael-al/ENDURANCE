#ifndef ENDURANCE_COLD_MARSH_SPHERES_GLSL
#define ENDURANCE_COLD_MARSH_SPHERES_GLSL
#include "height.glsl"
#include "sphere_settings.glsl"

struct ColdMarshSphereSite { vec2 center; uint key; };

ColdMarshSphereSite coldMarshSphereInCell(ivec2 cell)
{
    uint key = noiseInternalHash(uint(cell.x) * 0x9e3779b9u
        ^ uint(cell.y) * 0x85ebca6bu ^ uSeed ^ 0xc09d3f17u);
    vec2 jitter = vec2(noiseInternalUintTo01(noiseInternalHash(key ^ 0x7f4a7c15u)),
        noiseInternalUintTo01(noiseInternalHash(key ^ 0x94d049bbu)));
    return ColdMarshSphereSite((vec2(cell) + 0.25 + 0.5 * jitter)
        * COLD_MARSH_SPHERE_SPACING, key);
}

ColdMarshSphereSite coldMarshSphereSite(vec2 p)
{
    // Radius + 64-m fade fits within the 2500-m centre-to-cell-edge margin.
    return coldMarshSphereInCell(ivec2(floor(p / COLD_MARSH_SPHERE_SPACING)));
}

float coldMarshSphereFootprint(vec2 p, ColdMarshSphereSite site)
{
    return length(p - site.center) - COLD_MARSH_SPHERE_RADIUS;
}

float coldMarshSphereCenterHeight()
{
    return COLD_MARSH_SPHERE_RADIUS * (1.0 - 2.0 * COLD_MARSH_SPHERE_BURIED_FRACTION);
}

float coldMarshSphereAnchorSpan()
{
    // Same conservative macro/relief bounds as the terrain pipeline.
    return max(1.0, max(uHeightAmplitude, 0.0)
        + FIELD_MAX_RELIEF_HEIGHT * max(uDetailScale, 0.0));
}

float coldMarshSphereAnchorHeight(ColdMarshSphereSite site)
{
    return coldMarshWorldGround(site.center, coldMarshGroundHeight(site.center));
}

float coldMarshSphereAnchorControl(ColdMarshSphereSite site)
{
    // An absolute height makes the decoded sphere centre identical in every
    // column, unlike caching a separately rounded terrain-relative delta.
    return clamp(0.5 + 0.5 * (coldMarshSphereAnchorHeight(site) - uBaseHeight)
        / coldMarshSphereAnchorSpan(), 0.0, 1.0);
}

float coldMarshSphereAnchorFromControl(float control)
{
    return uBaseHeight + (2.0 * control - 1.0) * coldMarshSphereAnchorSpan();
}

float coldMarshSphereStoredAnchor(ColdMarshSphereSite site)
{
    float control = round(coldMarshSphereAnchorControl(site) * 65535.0) * (1.0 / 65535.0);
    return coldMarshSphereAnchorFromControl(control);
}

vec3 coldMarshSphereLocal(vec3 p, ColdMarshSphereSite site, float anchor)
{
    return p - vec3(site.center.x, anchor + coldMarshSphereCenterHeight(), site.center.y);
}

float coldMarshSphereEnvelope(vec3 q)
{
    return length(q) - COLD_MARSH_SPHERE_RADIUS;
}

float coldMarshSphereBand(vec2 p, ColdMarshSphereSite site, float anchor, float ground)
{
    if (uDetailScale <= 0.0) return 0.0;
    float offset = uBandOffset;
    if (uBandOffsetVariation > 0.0)
        offset += uBandOffsetVariation * noisePerlin2D(p * 0.0004, uSeed ^ 0xa4093822u);
    vec2 q = p - site.center;
    float expandedRadius = COLD_MARSH_SPHERE_RADIUS + 64.0;
    float reach = sqrt(max(0.0, expandedRadius * expandedRadius - dot(q, q)));
    float halfBand = abs(anchor + coldMarshSphereCenterHeight() - ground - offset)
        + reach + 80.0;
    return min(2.0 * halfBand / uDetailScale, FIELD_MAX_VOLUME_THICKNESS);
}
#endif
