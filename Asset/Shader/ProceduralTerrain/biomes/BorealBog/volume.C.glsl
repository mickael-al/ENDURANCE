#version 450 core
// Biome 4: BorealBog. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 4u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSite(p.xz, 2500.0, 507u, 0.55);
    if (site.mask <= 0.0) return 0.0;
    vec3 q = landVolumeLocal(p, column, site);
    float base = column.terrain.x - p.y;
    float r = site.radius;
    // An unusually long, low mineral island. Its blunt roof is a running area,
    // while the optional cavity is restricted to one shoreline-like flank.
    vec3 bank = q - vec3(0.0, 4.0, 0.0);
    bank.z -= r * 0.08 * sin(q.x / (r * 0.44));
    float solid = -landEllipsoid(bank, vec3(r * 0.72, 74.0, r * 0.31));
    if (site.variant > 0.55)
    {
        float spur = -landEllipsoid(q - vec3(-r * 0.32, -7.0, r * 0.12),
            vec3(r * 0.31, 53.0, r * 0.26));
        solid = landUnion(solid, spur, 15.0);
    }
    vec3 alcove = q - vec3(r * 0.10, 26.0, r * 0.28);
    float cavity = landEllipsoid(alcove, vec3(r * 0.31, 20.0, r * 0.27));
    cavity += noiseSimplex3D(q * 0.012, uSeed ^ 508u) * 1.0;
    float target = landUnion(base, solid * uDetailScale, 6.0 * uDetailScale);
    target = min(target, cavity * uDetailScale);
    return landDelta(p, column, target, site.mask);
}

#include "../../field_volume_accumulate.glsl"
