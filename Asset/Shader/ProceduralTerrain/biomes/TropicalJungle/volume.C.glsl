#version 450 core
// Biome 19: TropicalJungle. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 19u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// A buttressed tower pair and a single large dissolution portal per rare site.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,1600.0,1901u,0.68);
    if(s.mask<=0.0 || s.variant<0.34) return 0.0;
    vec3 q=landVolumeLocal(p,column,s)-vec3(-0.25*s.radius,0.0,-0.12*s.radius);
    float base=column.terrain.x-p.y;
    vec3 bent=q;
    bent.x-=0.0010*max(q.y,0.0)*max(q.y,0.0);
    float a=-landEllipsoid(bent-vec3(-20.0,77.0,0.0),vec3(64.0,150.0,66.0));
    float b=-landEllipsoid(q-vec3(60.0,42.0,16.0),vec3(48.0,107.0,52.0));
    float body=landUnion(a,b,19.0);
    float target=landUnion(base,body*uDetailScale,7.0*uDetailScale);
    if(s.variant>0.63)
    {
        float portal=landEllipsoid(q-vec3(17.0,26.0,15.0),vec3(31.0,42.0,130.0));
        target=min(target,portal*uDetailScale);
    }
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
