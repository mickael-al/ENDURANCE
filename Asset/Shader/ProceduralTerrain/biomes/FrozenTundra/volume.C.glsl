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
    if (site.mask <= 0.0) return 0.0;
    vec3 q = landVolumeLocal(p, column, site);
    float base = column.terrain.x - p.y;
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
    float target = landUnion(base, solid * uDetailScale, 6.0 * uDetailScale);
    target = min(target, cavity * uDetailScale);
    return landDelta(p, column, target, site.mask);
}

#include "../../field_volume_accumulate.glsl"
