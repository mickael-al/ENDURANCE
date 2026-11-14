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
    LandSite hole = landRkmSite(p.xz, 0.25, 511u);
    float mask = max(site.mask, hole.mask);
    if (mask <= 0.0) return 0.0;
    vec3 q = landVolumeLocal(p, column, site);
    float base = column.terrain.x - p.y;
    float r = site.radius;
    // An unusually long, low mineral island. Its blunt roof is a running area,
    // while the optional cavity is restricted to one shoreline-like flank.
    vec3 bank = q - vec3(0.0, 30.0, 0.0);
    bank.z -= r * 0.08 * sin(q.x / (r * 0.44));
    float solid = -landEllipsoid(bank, vec3(r * 0.72, 128.0, r * 0.31));
    if (site.variant > 0.55)
    {
        float spur = -landEllipsoid(q - vec3(-r * 0.32, -7.0, r * 0.12),
            vec3(r * 0.31, 53.0, r * 0.26));
        solid = landUnion(solid, spur, 15.0);
    }
    vec3 alcove = q - vec3(r * 0.10, 35.0, r * 0.26);
    float cavity = landEllipsoid(alcove, vec3(r * 0.42, 58.0, r * 0.33));
    cavity += noiseSimplex3D(q * 0.012, uSeed ^ 508u) * 1.0;
    float target = landUnion(base, solid * uDetailScale, 6.0 * uDetailScale);
    target = min(target, cavity * uDetailScale);
    target = mix(base, target, site.mask / max(mask, 0.000001));
    if (hole.mask > 0.0)
    {
        vec3 c = landVolumeLocal(p, column, hole);
        // Perlin-deformed subsurface chamber, with a throat that explicitly
        // reaches above the heightfield. A sealed noise pocket is not an entrance.
        float chamber = landEllipsoid(c - vec3(15.0, -56.0, 0.0),
                                      vec3(72.0, 43.0, 48.0));
        float throat = landCapsule(c, vec3(-23.0, 10.0, 0.0),
                                  vec3(12.0, -53.0, 0.0), 22.0);
        float excavation = min(chamber, throat);
        excavation += noisePerlin3D(c * vec3(0.025, 0.035, 0.025), uSeed ^ 512u) * 5.0;
        excavation += noisePerlin3D(c * 0.080, uSeed ^ 513u) * 1.2;
        target = mix(target, min(target, excavation * uDetailScale), hole.mask / max(mask, 0.000001));
    }
    return landDelta(p, column, target, mask);
}

#include "../../field_volume_accumulate.glsl"
