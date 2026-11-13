#version 450 core
// Biome 8: ColdForest. Multiscale terrain with sparse, bounded sculptural sites.
// Positive density is solid; return only this biome's signed density delta.
#define FIELD_BIOME_ID 8u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    LandSite s=landSite(p.xz,2400.0,807u,.63);
    if(s.mask<=0.0 || !landVolumeEnabled(column)) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // A hanging shoulder on the uphill side leaves the cirque mouth open.
    vec3 a=q-vec3(.29*s.radius,42.0,0.0);
    a.x-=.22*a.y;
    float rock=-landEllipsoid(a,vec3(.19*s.radius,145.0,.42*s.radius));
    float balcony=-landBox(q-vec3(.12*s.radius,70.0,.14*s.radius),
                          vec3(.19*s.radius,29.0,.26*s.radius));
    rock=landUnion(rock,balcony,14.0);
    rock+=noiseSimplex3D(q*.012,uSeed^808u)*2.2;
    float target=max(base,rock*uDetailScale);
    float alcove=landEllipsoid(q-vec3(.09*s.radius,20.0,.02*s.radius),
                              vec3(.23*s.radius,45.0,.23*s.radius));
    target=min(target,alcove*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
