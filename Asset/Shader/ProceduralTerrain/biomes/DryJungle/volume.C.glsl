#version 450 core
// Biome 18: DryJungle. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 18u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// One tilted shelf on a table edge; undercut mouths stay lateral and local.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2200.0,1801u,0.65);
    if(s.mask<=0.0 || s.variant<0.38) return 0.0;
    vec3 q=landVolumeLocal(p,column,s)-vec3(-0.36*s.radius,0.0,0.0);
    float base=column.terrain.x-p.y;
    vec3 shelf=q-vec3(0.0,38.0,0.0);
    shelf.y-=0.26*shelf.x;
    float slab=-landBox(shelf,vec3(81.0,31.0,118.0));
    float root=-landEllipsoid(q-vec3(47.0,-12.0,0.0),vec3(60.0,92.0,120.0));
    float target=landUnion(base,landUnion(slab,root,15.0)*uDetailScale,5.0*uDetailScale);
    if(s.variant>0.65)
    {
        float cavity=landEllipsoid(q-vec3(-42.0,4.0,22.0),vec3(66.0,27.0,54.0));
        target=min(target,cavity*uDetailScale);
    }
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
