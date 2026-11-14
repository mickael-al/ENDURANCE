#version 450 core
// Biome 11: Meadow. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 11u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// A low stone curl on the amphitheatre flank; the centre stays open.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2400.0,11110u,.55);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // The same family ranges from a long low shoulder to an upright curl.
    float rise=mix(46.0,92.0,s.variant);
    vec3 shoulder=q-vec3(-.38*s.radius+mix(.06,.22,s.variant)*q.y,22.0,.13*s.radius);
    float d=landEllipsoid(shoulder,vec3(.22*s.radius,rise,mix(.43,.24,s.variant)*s.radius));
    d+=2.0*noiseSimplex3D(q*.012,uSeed^11111u);
    float target=max(base,-d*uDetailScale);
    vec3 hollow=q-vec3(-.22*s.radius,12.0,.15*s.radius);
    float cave=landEllipsoid(hollow,vec3(.16*s.radius,29.0,.19*s.radius));
    target=min(target,cave*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
