#version 450 core
// Biome 1: FrozenTundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSite(p.xz, 2200.0, 207u, 0.62);
    LandSite cave = landRkmSite(p.xz, 2.0, 209u);
    float support = max(site.mask, cave.mask);
    if (support <= 0.0) return 0.0;
    float base = column.terrain.x - p.y;
    float target = base;
    if (site.mask > 0.0)
    {
        vec3 q = landVolumeLocal(p, column, site);
        float r = site.radius;
        // An asymmetric pingo cap; a low end remains a ramp onto its broad roof.
        vec3 cap = q;
        cap.y -= 26.0 + 0.11 * q.x;
        float solid = -landEllipsoid(cap, vec3(r * 0.53, 102.0, r * 0.47));
        // Two site compositions: round cap or a broad tilted secondary plate.
        if (site.variant > 0.48)
        {
            vec3 plate = q - vec3(-r * 0.19, 8.0, r * 0.04);
            plate.y -= 0.14 * plate.x;
            solid = landUnion(solid,
                -landEllipsoid(plate, vec3(r * 0.42, 66.0, r * 0.30)), 16.0);
        }
        // Open the side of the pingo, leaving a continuous roof and ground floor.
        vec3 chamber = q - vec3(r * 0.12, 39.0, r * 0.30);
        float cavity = landEllipsoid(chamber, vec3(r * 0.28, 30.0, r * 0.35));
        cavity += noiseSimplex3D(q * 0.014, uSeed ^ 208u) * 1.7;
        float pingoTarget = landUnion(base, solid * uDetailScale, 6.0 * uDetailScale);
        pingoTarget = min(pingoTarget, cavity * uDetailScale);
        // Keep a surface opening even where a pingo population intersects the
        // underground caves; otherwise a cap could seal the independent portal.
        target = mix(base, pingoTarget, site.mask * (1.0 - cave.mask) / support);
    }
    if (cave.mask > 0.0)
    {
        vec3 q = landVolumeLocal(p, column, cave);
        vec3 center = vec3(0.0, -42.0, 0.0);
        float chamber = landEllipsoid(q - center, vec3(22.0, 24.0, 18.0));
        vec3 mouth = vec3(cave.radius * 0.50, 12.0, cave.radius * 0.10);
        float corridor = landCapsule(q, center, mouth, 9.0);
        float hollow = min(chamber, corridor);
        hollow += noiseSimplex3D(vec3(q.x, q.y * 0.35, q.z) * 0.07,
            uSeed ^ 210u) * 1.2;
        target = mix(target, min(target, hollow * uDetailScale), cave.mask / support);
    }
    return landDelta(p, column, target, support);
}

#include "../../field_volume_accumulate.glsl"
