#version 450 core
// Biome 6: ColdSteppe. Multiscale terrain with sparse, bounded sculptural sites.
// Positive density is solid; return only this biome's signed density delta.
#define FIELD_BIOME_ID 6u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    LandSite s=landSite(p.xz,2700.0,606u,.52);
    if(s.mask<=0.0 || !landVolumeEnabled(column)) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // One low, elongated rock shoulder ends in a beak; broad open steppe lies
    // outside this rare site rather than being riddled with generic tubes.
    vec3 a=q-vec3(.12*s.radius,19.0,0.0);
    a.x-=.30*a.y;
    float rock=-landEllipsoid(a,vec3(.43*s.radius,63.0,.125*s.radius));
    rock+=noiseSimplex3D(q*vec3(.012,.018,.008),uSeed^607u)*1.5;
    float target=max(base,rock*uDetailScale);
    float opening=landEllipsoid(q-vec3(.37*s.radius,7.0,.04*s.radius),
                               vec3(.17*s.radius,31.0,.18*s.radius));
    target=min(target,opening*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
