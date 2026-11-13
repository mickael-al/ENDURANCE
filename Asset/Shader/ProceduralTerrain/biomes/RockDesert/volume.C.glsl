#version 450 core
// Biome 15: RockDesert. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 15u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// A leaning, tapered wind-carved blade and a rare large side opening.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2100.0,1501u,0.64);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    float target=base;
    if(s.variant>0.34)
    {
        vec3 blade=q-vec3(-0.20*s.radius,75.0,0.0);
        blade.x-=0.0024*max(q.y,0.0)*max(q.y,0.0);
        float wall=landEllipsoid(blade,vec3(32.0,150.0,0.54*s.radius));
        target=landUnion(base,-wall*uDetailScale,5.0*uDetailScale);
        if(s.variant>0.70)
        {
            vec3 aperture=q-vec3(-0.20*s.radius+10.0,24.0,-0.05*s.radius);
            float opening=landEllipsoid(aperture,vec3(100.0,36.0,48.0));
            target=min(target,opening*uDetailScale);
        }
    }
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
