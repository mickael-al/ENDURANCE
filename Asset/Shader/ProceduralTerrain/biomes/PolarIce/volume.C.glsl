#version 450 core
// Biome 0: PolarIce. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 0u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSite(p.xz, 2400.0, 107u, 0.70);
    LandSite rift = landRkmSite(p.xz, 0.50, 109u);
    float support = max(site.mask, rift.mask);
    if (support <= 0.0) return 0.0;
    float base = column.terrain.x - p.y;
    float target = base;
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
        // A bevelled slab gives ice a broad planar roof instead of another dome.
        float ice = 18.0 - landBox(shelf, vec3(r * 0.50, 72.0, r * 0.26));
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
        float shelfTarget = landUnion(base, ice * uDetailScale, 6.0 * uDetailScale);
        shelfTarget = min(shelfTarget, cavity * uDetailScale);
        // The old nappe must not grow back over the floor of a new tear.
        float bend = 18.0 * sin(rift.q.x / 120.0 + rift.variant * 6.2831853)
            + 8.0 * sin(rift.q.x / 57.0 + rift.variant * 2.0);
        float halfWidth = mix(80.0, 105.0, rift.variant);
        float tear = rift.mask
            * (1.0 - smoothstep(0.62, 0.94, abs(rift.q.x) / rift.radius))
            * (1.0 - smoothstep(halfWidth - 8.0, halfWidth + 8.0, abs(rift.q.y - bend)));
        target = mix(base, shelfTarget, site.mask * (1.0 - tear) / support);
    }
    if (rift.mask > 0.0)
    {
        vec3 q = landVolumeLocal(p, column, rift);
        float x = -rift.radius * 0.18;
        float z = 18.0 * sin(x / 120.0 + rift.variant * 6.2831853)
            + 8.0 * sin(x / 57.0 + rift.variant * 2.0);
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
    return landDelta(p, column, target, support);
}

#include "../../field_volume_accumulate.glsl"
