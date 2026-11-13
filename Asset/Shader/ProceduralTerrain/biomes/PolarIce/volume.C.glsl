#version 450 core
// Biome 0: PolarIce. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 0u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "shape.glsl"

// Coarse ledges plus smaller fractures; all coordinates are metres, including Y.
// Clamp each noise so the 2D swept-wall envelope remains conservative.
vec2 polarRiftWallWarp(vec3 p, LandSite rift)
{
    vec3 q = vec3(rift.q.x, p.y, rift.q.y);
    vec3 broad = q / vec3(180.0, 90.0, 130.0);
    vec3 detail = q / vec3(55.0, 28.0, 45.0);
    vec2 coarseNoise = clamp(vec2(noiseSimplex3D(broad, uSeed ^ 119u),
        noiseSimplex3D(broad + vec3(17.1, 31.7, 9.2), uSeed ^ 120u)), -1.0, 1.0);
    vec2 detailNoise = clamp(vec2(noiseSimplex3D(detail, uSeed ^ 121u),
        noiseSimplex3D(detail + vec3(8.3, 12.6, 29.1), uSeed ^ 122u)), -1.0, 1.0);
    return max(POLAR_RIFT_WALL_STRENGTH, 0.0)
        * (coarseNoise * POLAR_RIFT_WALL_WARP + detailNoise * POLAR_RIFT_WALL_DETAIL);
}

float polarRiftWallDelta(vec3 p, FieldColumn column, LandSite rift)
{
    if (polarRiftWallCoverage(rift) <= 0.0 || rift.mask <= 0.08) return 0.0;
    float weight = fieldSurfaceWeight(column, FIELD_BIOME_ID);
    if (weight <= 0.0) return 0.0;
    float depth = polarRiftDepth(rift) * (uDetailScale / 0.35);
    float originalCut = polarRiftMask(rift);
    // Undo only this biome's weighted incision to locate its blended rim.
    // The returned delta is unweighted; accumulation applies the weight once.
    float rim = column.terrain.x + column.blend.y + weight * originalCut * depth;
    float depthFraction = (rim - p.y) / max(weight * depth, 0.0001);
    float vertical = smoothstep(0.04, 0.18, depthFraction)
        * (1.0 - smoothstep(0.82, 0.96, depthFraction));
    if (vertical <= 0.0) return 0.0;
    LandSite displaced = rift;
    displaced.q += polarRiftWallWarp(p, rift) * vertical
        * smoothstep(0.08, 0.20, rift.mask);
    // Keep the compact population mask fixed; distort its internal contour.
    // Unlike height noise, this moves the cliff sideways as Y changes and can
    // create overhangs. Both the mouth and the floor return to the original cut.
    return (originalCut - polarRiftMask(displaced)) * depth;
}

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSparseSite(p.xz, 9600.0, 0.25, 107u, 0.65);
    LandSite rift = landSparseSite(p.xz, POLAR_RIFT_SPACING, POLAR_RIFT_FOOTPRINT,
        POLAR_RIFT_SALT, POLAR_RIFT_OCCUPANCY);
    float support = max(site.mask, rift.mask);
    if (support <= 0.0) return 0.0;
    float base = column.terrain.x - p.y;
    float wallDelta = polarRiftWallDelta(p, column, rift);
    float sculptedBase = base + wallDelta;
    float target = sculptedBase;
    if (site.mask > 0.0)
    {
        vec3 q = landVolumeLocal(p, column, site);
        float r = site.radius;
        // Bend and taper the whole formation in XZ, including its tunnel. A cheap
        // continuous coordinate warp breaks the rectangular footprint while
        // leaving the broad roof and vertical envelope intact. The narrow end and
        // gently bowed front differ from the fuller, curved rear of the ice nappe.
        float along = clamp(q.x / r, -1.0, 1.0);
        q.z -= r * (0.20 * along * along - 0.045 * along);
        float across = q.z / r;
        q.x += r * 0.70 * across * across;
        q.z /= 1.0 - 0.42 * smoothstep(-0.45, 0.55, along);
        // A thick ice nappe rises gradually, with a raised lip at one end.
        // All additive pieces intersect the ground; there are no floating shells.
        float fold = 12.0 * sin(q.x / (r * 0.30) + site.variant * 4.0);
        vec3 shelf = q - vec3(0.0, 28.0 + fold, 0.0);
        shelf.y -= 0.045 * shelf.x;
        // Keep the broad planar roof, but give the nappe a rounded, irregular
        // shoreline so its tunnel no longer opens through a rectangular slab.
        float rim = length(shelf.xz / vec2(r * 0.55, r * 0.31));
        rim += (landPlacementValue(q.xz / (r * 0.19), 118u) - 0.5) * 0.055;
        float side = (1.0 - rim) * r * 0.31;
        float cap = 90.0 - abs(shelf.y);
        float ice = -landUnion(-side, -cap, 8.0);
        float lip = -landCapsule(q, vec3(-r * 0.30, 52.0, -r * 0.18),
            vec3(r * 0.29, 67.0, -r * 0.15), 78.0);
        ice = landUnion(ice, lip, 20.0);
        // A single broad, lateral tunnel: its floor stays above the ordinary field.
        // Variant changes its curve and dimensions, not the number of noisy holes.
        vec3 tunnel = q - vec3(0.0, 38.0, r * 0.24);
        tunnel.y -= 7.0 * sin(q.x / (r * 0.25));
        float cavity = landEllipsoid(tunnel,
            vec3(r * mix(0.38, 0.51, site.variant), 30.0, r * 0.29));
        cavity += noiseSimplex3D(q * 0.018, uSeed ^ 108u) * 1.6;
        float shelfTarget = landUnion(sculptedBase, ice * uDetailScale, 6.0 * uDetailScale);
        shelfTarget = min(shelfTarget, cavity * uDetailScale);
        // The old nappe must not grow back over the floor of a new tear.
        float tear = polarRiftMask(rift);
        target = mix(sculptedBase, shelfTarget, site.mask * (1.0 - tear) / support);
    }
    if (rift.mask > 0.0)
    {
        vec3 q = landVolumeLocal(p, column, rift);
        float x = -rift.radius * 0.18;
        float z = polarRiftCenterline(x, rift);
        // A small buried floor chamber (roof ~6 m below the rift floor),
        // connected to the open floor by a sloping, person-sized entrance.
        vec3 center = vec3(x, -42.0, z);
        float chamber = landEllipsoid(q - center, vec3(30.0, 24.0, 22.0));
        float entry = landCapsule(q, center, center + vec3(68.0, 55.0, 16.0), 10.0);
        float hollow = min(chamber, entry);
        hollow += noiseSimplex3D(vec3(q.x, q.y * 0.35, q.z) * 0.06,
            uSeed ^ 110u) * 1.5;
        target = mix(target, min(target, hollow * uDetailScale), rift.mask / support);
    }
    // Displace the terrain BEFORE CSG: adding a negative wall delta after the
    // cave's min() could excavate its positive exterior distance field as well.
    // The cut already includes rift.mask; apply support only to the extra CSG.
    float finalTarget = sculptedBase + support * (target - sculptedBase);
    return landDelta(p, column, finalTarget, 1.0);
}

#include "../../field_volume_accumulate.glsl"
