#version 450 core
// Biome 16: DrySavanna. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 16u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// Three joined weathered bodies: one landmark, not boulders across the floor.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,1800.0,1601u,0.60);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    float target=base;
    if(s.variant>0.40)
    {
        vec3 r=q-vec3(-0.22*s.radius,0.0,-0.10*s.radius);
        float a=-landEllipsoid(r-vec3(-21.0,70.0,0.0),vec3(58.0,120.0,52.0));
        float b=-landEllipsoid(r-vec3(35.0,33.0,19.0),vec3(47.0,83.0,49.0));
        float c=-landEllipsoid(r-vec3(8.0,137.0,-9.0),vec3(51.0,58.0,43.0));
        float group=landUnion(landUnion(a,b,13.0),c,11.0);
        target=landUnion(base,group*uDetailScale,6.0*uDetailScale);
        if(s.variant>0.76)
        {
            float porch=landEllipsoid(r-vec3(23.0,14.0,37.0),vec3(25.0,26.0,65.0));
            target=min(target,porch*uDetailScale);
        }
    }
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
