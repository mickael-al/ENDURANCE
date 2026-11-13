#version 450 core
// Biome 10: DryPlain. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 10u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// A rare elongated keel or a rounded butte, with a single side alcove.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2600.0,10110u,.60);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    vec3 rock=q-vec3(.20*q.y,28.0,0.0);
    vec3 radius=vec3(mix(.47,.26,s.variant)*s.radius,mix(64.0,108.0,s.variant),.17*s.radius);
    float d=landEllipsoid(rock,radius);
    d+=2.0*noiseSimplex3D(q*.014,uSeed^10111u);
    float target=max(base,-d*uDetailScale);
    vec3 c=q-vec3(.08*s.radius,12.0,.17*s.radius);
    float alcove=landEllipsoid(c,vec3(.20*s.radius,35.0,.15*s.radius));
    target=min(target,alcove*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
