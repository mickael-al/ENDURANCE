#ifndef ENDURANCE_TAIGA_PILLARS_GLSL
#define ENDURANCE_TAIGA_PILLARS_GLSL
#include "height.glsl"
#include "pillar_sites.glsl"
#include "pillar_porosity.glsl"
#include "../../biome_landforms_3d.glsl"

float taigaPillarLength(TaigaPillarSite site)
{
    // height is the VERTICAL maximum, including the tilted circular top rim.
    return (site.height - site.radius * sin(site.tilt)) / cos(site.tilt);
}

float taigaPillarFootprint(vec2 p, TaigaPillarSite site)
{
    vec2 direction = vec2(cos(site.azimuth), sin(site.azimuth));
    vec2 end = direction * (taigaPillarLength(site) * sin(site.tilt));
    vec2 q = p - site.center;
    float t = clamp(dot(q, end) / max(dot(end, end), 0.00001), 0.0, 1.0);
    return length(q - end * t) - site.radius;
}

float taigaWorldHeight(vec2 p, float rawHeight)
{
    float macro = uBaseHeight;
    if (uHeightAmplitude > 0.0)
        macro += noiseSimplexFbm2D(p * uHeightFrequency,
            uSeed ^ 0x13198a2eu, 5, 2.0, 0.5) * uHeightAmplitude;
    precise float height = macro + clamp(rawHeight,
        -FIELD_MAX_RELIEF_HEIGHT, FIELD_MAX_RELIEF_HEIGHT) * uDetailScale;
    return height;
}

float taigaPillarAnchorHeight(TaigaPillarSite site)
{
    return taigaWorldHeight(site.center, taigaBaseRelief(site.center).x);
}

vec3 taigaPillarLocal(vec3 p, TaigaPillarSite site, float anchorHeight)
{
    float st = sin(site.tilt), ct = cos(site.tilt);
    float sa = sin(site.azimuth), ca = cos(site.azimuth);
    vec3 axis = vec3(st * ca, ct, st * sa);
    vec3 across = vec3(ct * ca, -st, ct * sa);
    vec3 side = vec3(-sa, 0.0, ca);
    vec3 q = p - vec3(site.center.x, anchorHeight, site.center.y);
    return vec3(dot(q, across), dot(q, axis), dot(q, side));
}

float taigaPillarEnvelope(vec3 local, TaigaPillarSite site)
{
    float lengthAxis = taigaPillarLength(site);
    vec2 d = vec2(length(local.xz) - site.radius,
        abs(local.y - 0.5 * lengthAxis) - 0.5 * lengthAxis);
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

float taigaPillarGeometry(vec3 p, TaigaPillarSite site, float anchorHeight)
{
    vec3 q = taigaPillarLocal(p, site, anchorHeight);
    float envelope = taigaPillarEnvelope(q, site);
    if (envelope >= 32.0) return -envelope;
    vec3 normalized = q / vec3(site.radius, taigaPillarLength(site), site.radius);
    // Intersect a perfect cylinder with 70% stone / 30% Perlin void.
    // No displacement can push material outside the requested envelope.
    float stone = taigaPillarPoreField(normalized, site.key) * 100.0;
    return min(-envelope, stone);
}

float taigaPillarBand(vec2 p, float rawHeight)
{
    if (uFieldMode != 0u || uDetailScale <= 0.0) return 0.0;
    float surface = taigaWorldHeight(p, rawHeight);
    float offset = uBandOffset;
    if (uBandOffsetVariation > 0.0)
        offset += uBandOffsetVariation * noisePerlin2D(p * 0.0004, uSeed ^ 0xa4093822u);
    float band = 0.0;
    ivec2 cell = taigaPillarGrid(p);
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 bin = cell + ivec2(x, z);
        int count = taigaPillarBinCount(bin);
        for (int i = 0; i < count; ++i)
        {
            TaigaPillarSite site = taigaPillarInBin(bin, i);
            float edge = taigaPillarFootprint(p, site);
            if (edge >= 64.0) continue;
            float anchor = taigaPillarAnchorHeight(site);
            float low = anchor - site.radius * sin(site.tilt) - surface - offset;
            float high = anchor + site.height - surface - offset;
            float width = 2.0 * (max(abs(low), abs(high)) + 64.0) / uDetailScale;
            // Full dispatch support beyond the density's 32-m fade.
            width *= 1.0 - smoothstep(40.0, 64.0, edge);
            band = max(band, width);
        }
    }
    return min(band, FIELD_MAX_VOLUME_THICKNESS);
}
#endif
