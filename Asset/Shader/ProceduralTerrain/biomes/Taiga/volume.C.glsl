#version 450 core
// Biome 3: Taiga. World XZ is in metres; relief/vertical shape units
// are scaled by uDetailScale. Site spacing, footprint and detail are independent.
#define FIELD_BIOME_ID 3u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if (!landVolumeEnabled(column)) return 0.0;
    LandSite site = landSite(p.xz, 2800.0, 407u, 0.65);
    if (site.mask <= 0.0) return 0.0;
    vec3 q = landVolumeLocal(p, column, site);
    float base = column.terrain.x - p.y;
    float r = site.radius;
    // Bent, oblong backbone. Both ends taper into broad terrain ramps.
    vec3 backbone = q;
    backbone.z -= r * 0.13 * sin(q.x / (r * 0.43) + site.variant * 2.0);
    // Compress the metric along its axis: the two capsule ends become a long
    // low ramp and a raised blunt head, rather than a symmetric round dome.
    backbone.x *= 0.40;
    float solid = -landCapsule(backbone, vec3(-r * 0.12, -10.0, 0.0),
        vec3(r * 0.12, 50.0, 0.0), 70.0);
    float shoulder = -landEllipsoid(q - vec3(-r * 0.19, 0.0, -r * 0.10),
        vec3(r * 0.43, 62.0, r * 0.28));
    solid = landUnion(solid, shoulder, 22.0);
    // The gap is on one flank; most of the crest and its climbing ramp survive.
    vec3 cleft = q - vec3(r * 0.20, 38.0, r * 0.16);
    cleft.y -= 8.0 * sin(q.x / (r * 0.24));
    float cavity = landEllipsoid(cleft, vec3(r * 0.25, 27.0, r * 0.24));
    cavity += noiseSimplex3D(q * 0.012, uSeed ^ 408u) * 2.0;
    float target = landUnion(base, solid * uDetailScale, 6.0 * uDetailScale);
    target = min(target, cavity * uDetailScale);
    return landDelta(p, column, target, site.mask);
}

#include "../../field_volume_accumulate.glsl"
