#version 450 core
// Biome 9: ColdMarsh. Multiscale terrain with sparse, bounded sculptural sites.
// Positive density is solid; return only this biome's signed density delta.
#define FIELD_BIOME_ID 9u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    LandSite s=landSite(p.xz,2300.0,905u,.58);
    if(s.mask<=0.0 || !landVolumeEnabled(column)) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // Low, long bank lips dominate this wetland's 3D; the centre stays clear.
    float bank=-landEllipsoid(q-vec3(0.0,4.0,.39*s.radius),
                             vec3(.38*s.radius,34.0,.13*s.radius));
    float second=-landEllipsoid(q-vec3(.13*s.radius,0.0,-.40*s.radius),
                               vec3(.28*s.radius,25.0,.10*s.radius));
    float target=max(base,max(bank,second)*uDetailScale);
    float undercut=landEllipsoid(q-vec3(.06*s.radius,-1.0,.30*s.radius),
                                vec3(.21*s.radius,13.0,.105*s.radius));
    target=min(target,undercut*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
