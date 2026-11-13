#version 450 core
// Biome 17: Savanna. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 17u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// A rare asymmetric crown/alcove leaves the long ascent slopes clear.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2800.0,1701u,0.62);
    if(s.mask<=0.0 || s.variant<0.62) return 0.0;
    vec3 q=landVolumeLocal(p,column,s)-vec3(-0.23*s.radius,0.0,-0.08*s.radius);
    float base=column.terrain.x-p.y;
    float crown=-landEllipsoid(q-vec3(22.0,57.0,0.0),vec3(95.0,132.0,70.0));
    float buttress=-landCapsule(q,vec3(-48.0,-60.0,-12.0),vec3(11.0,80.0,-5.0),33.0);
    float solid=landUnion(crown,buttress,18.0);
    float target=landUnion(base,solid*uDetailScale,8.0*uDetailScale);
    float alcove=landEllipsoid(q-vec3(78.0,17.0,23.0),vec3(68.0,35.0,39.0));
    target=min(target,alcove*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
