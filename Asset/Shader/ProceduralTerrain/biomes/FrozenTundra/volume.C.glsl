#version 450 core
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "caves.glsl"

// This is the same macro height used by field_batch_classify. Evaluated only
// inside a rare eligible cave; never recompute the expensive mountain field.
float frozenCaveMacroHeight(vec2 p)
{
    float h = uBaseHeight;
    if (uHeightAmplitude > 0.0)
        h += noiseSimplexFbm2D(p * uHeightFrequency,
            uSeed ^ 0x13198a2eu, 5, 2.0, 0.5) * uHeightAmplitude;
    return h;
}

// Positive outside the air volume. Reference-metre coordinates reuse the
// initial surface-relative 3D machinery: chamber roofs/floors and the gallery
// follow the broad terrain elevation, rather than being a fixed horizontal
// world-space cave. The enclosed volume and its connected entrance are 3D.
vec2 frozenCaveFields(vec3 q, LandSite site, float depth)
{
    vec3 chamberCenter = frozenCaveChamberCenter(site, depth);
    vec3 chamberRadii = frozenCaveChamberRadii(site);
    float chamber = landEllipsoid(q - chamberCenter, chamberRadii);
    vec3 sideCenter = vec3(165.0, -depth - 24.0, 80.0);
    float sideRoom = landEllipsoid(q - sideCenter,
        vec3(135.0, 57.0, 105.0));
    float excavation = -landUnion(-chamber, -sideRoom, 22.0);

    // Overlapping capsules deliberately connect the buried rooms to air above
    // the surface. Noise alone would often create sealed pockets, not caves.
    vec3 bendA = vec3(105.0, -depth * 0.78, -55.0);
    vec3 bendB = vec3(390.0, -depth * 0.34, 65.0);
    float radius = mix(33.0, 45.0, site.variant);
    float gallery = min(landCapsule(q, chamberCenter, bendA, radius),
        min(landCapsule(q, bendA, bendB, radius),
            landCapsule(q, bendB, frozenCaveEntrance(), radius * 1.12)));
    gallery = min(gallery, landCapsule(q, chamberCenter, sideCenter, 38.0));
    // Broad 3D irregularities keep the guaranteed centerline open: combined
    // displacement <=10 m, versus a minimum tunnel radius of 33 m.
    float deformation = noisePerlin3D(q / vec3(150.0, 95.0, 150.0)
        + site.variant * vec3(13.1, 23.7, 41.9), uSeed ^ 331u) * 8.0;
    deformation += noisePerlin3D(q / vec3(55.0, 45.0, 55.0), uSeed ^ 332u) * 2.0;
    return vec2(excavation, gallery) + deformation;
}

float frozenCaveExcavation(vec3 q, LandSite site, float depth)
{
    vec2 cavities = frozenCaveFields(q, site, depth);
    return min(cavities.x, cavities.y);
}

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = frozenCaveSite(p.xz);
    if (site.mask <= 0.0) return 0.0;
    float presence = frozenMountainPresence(p.xz);
    float support = frozenCaveSupport(p.xz, site, presence);
    float biomeGate = smoothstep(0.75, 0.98, fieldSurfaceWeight(column, FIELD_BIOME_ID));
    support *= biomeGate;
    if (support <= 0.0) return 0.0;
    float scale = uDetailScale / 0.35;
    float macro = frozenCaveMacroHeight(p.xz);
    // The plains have a reference upper bound <60 m. Infer a conservative
    // mountain thickness from the final column without decoding blended pilot
    // data or recomputing any mountain site. Keep a root above that substrate.
    float available = (column.terrain.x - macro) / scale - FROZEN_CAVE_PLAIN_CEILING;
    float chamberGate = frozenCaveHeightGate(available);
    float galleryGate = frozenCaveGalleryHeightGate(available);
    if (max(chamberGate, galleryGate) <= 0.0) return 0.0;
    float depth = frozenCaveDepth(site, available);
    vec3 q = landVolumeLocal(p, column, site);
    q.y *= 0.35;
    vec2 cavities = frozenCaveFields(q, site, depth);
    float protectedFloor = macro + scale * (FROZEN_CAVE_PLAIN_CEILING + FROZEN_CAVE_BEDROCK);
    cavities = max(cavities, vec2((protectedFloor - p.y) / scale));
    float base = column.terrain.x - p.y;
    float chamberTarget = mix(base, min(base, cavities.x * scale), chamberGate);
    float galleryTarget = mix(base, min(base, cavities.y * scale), galleryGate);
    float target = min(chamberTarget, galleryTarget);
    return landDelta(p, column, target, support);
}

#include "../../field_volume_accumulate.glsl"
