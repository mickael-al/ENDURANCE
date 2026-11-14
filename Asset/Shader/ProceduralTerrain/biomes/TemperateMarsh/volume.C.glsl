#version 450 core
// Biome 14: TemperateMarsh. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 14u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// Raised fossil riverbank with a shallow overhanging lip, open on one side.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2500.0,14110u,.58);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // Sweep a broken curved bank; the missing arc is a wide ground-level exit.
    float radial=length(q.xz*vec2(1.0,mix(.76,1.20,s.variant)))-.43*s.radius;
    vec3 section=vec3(radial,q.y-19.0,q.z*.12);
    float bank=-landEllipsoid(section,vec3(.12*s.radius,50.0,.35*s.radius));
    bank=min(bank,-q.x-.05*s.radius);
    float target=max(base,bank*uDetailScale);
    float hollow=length(vec2((radial+.065*s.radius)/(.09*s.radius),(q.y-8.0)/18.0))-1.0;
    hollow*=18.0;
    // Keep the cut limited to the bank's left arc, away from the open route.
    hollow=max(hollow,q.x+.12*s.radius);
    target=min(target,hollow*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
