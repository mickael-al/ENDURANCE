#version 450 core
// Biome 2: Tundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 2u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSite(p.xz, 2600.0, 307u, 0.58);
    if (site.mask <= 0.0) return 0.0;
    vec3 q = landVolumeLocal(p, column, site);
    float base = column.terrain.x - p.y;
    float r = site.radius;
    // A long reclining brow. One end curls upward; the opposite end is buried.
    vec3 spine = q;
    spine.z -= 0.14 * r * sin(q.x / (r * 0.50));
    spine.y -= mix(0.0, 75.0, smoothstep(-r * 0.55, r * 0.50, q.x));
    float rock = -landEllipsoid(spine, vec3(r * 0.69, 76.0, r * 0.23));
    rock += noiseSimplex3D(q * 0.010, uSeed ^ 308u) * 2.0;
    // Sheltered recess below the rising end, never a pervasive ground fissure.
    vec3 shelter = q - vec3(r * 0.27, 35.0, r * 0.18);
    float cavity = landEllipsoid(shelter, vec3(r * 0.27, 26.0, r * 0.24));
    float target = landUnion(base, rock * uDetailScale, 6.0 * uDetailScale);
    target = min(target, cavity * uDetailScale);
    return landDelta(p, column, target, site.mask);
}

#include "../../field_volume_accumulate.glsl"
