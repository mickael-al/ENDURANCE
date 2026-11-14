#version 450 core
// Biome 2: Tundra. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 2u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"
#include "caves.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSite(p.xz, 2600.0, 307u, 0.58);
    LandSite rocks = landRkmSite(p.xz, 0.5, 322u);
    rocks.mask *= 1.0 - smoothstep(27.0, 42.0, length(rocks.q));
    if (max(site.mask, rocks.mask) <= 0.0) return 0.0;
    float base = column.terrain.x - p.y;
    float target = base;
    if (site.mask > 0.0)
    {
        vec3 q = landVolumeLocal(p, column, site);
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
        float oldTarget = landUnion(base, rock * uDetailScale, 6.0 * uDetailScale);
        oldTarget = min(oldTarget, cavity * uDetailScale);
        target = mix(base, oldTarget, site.mask);
    }
    if (rocks.mask > 0.0)
    {
        vec3 b = landVolumeLocal(p, column, rocks);
        // Grounded angular erratic: raw Y rises to 20/.35, XZ stays metres.
        b.y -= 18.0;
        b.x += 0.055 * b.y;
        float stone = -landEllipsoid(b, vec3(17.0, 20.0 / 0.35 - 18.0, 13.0));
        // Chipped faces and low-amplitude grain preserve the large silhouette.
        stone = min(stone, (18.0 - abs(b.x + b.z * 0.40)) * 0.8);
        stone += noiseSimplex3D(b * vec3(0.13, 0.055, 0.13), uSeed ^ 324u) * 0.65;
        float withStone = landUnion(target, stone * uDetailScale, 1.2 * uDetailScale);
        target = mix(target, withStone, rocks.mask);
    }
    target = tundraCarveCaves(p, column, site, target);
    // Individual site masks have already been applied exactly once above.
    return landDelta(p, column, target, 1.0);
}

#include "../../field_volume_accumulate.glsl"
