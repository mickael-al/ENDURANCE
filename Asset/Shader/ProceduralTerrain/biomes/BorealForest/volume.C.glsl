#version 450 core
// Biome 7: BorealForest. Multiscale terrain with sparse, bounded sculptural sites.
// Positive density is solid; return only this biome's signed density delta.
#define FIELD_BIOME_ID 7u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    LandSite s=landSite(p.xz,1850.0,705u,.70);
    if(s.mask<=0.0 || !landVolumeEnabled(column)) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // Coupled polished whaleback masses, not identical spherical stones.
    float rock=-landEllipsoid(q-vec3(-.13*s.radius,22.0,0.0),
                             vec3(.37*s.radius,109.0,.25*s.radius));
    float shoulder=-landEllipsoid(q-vec3(.22*s.radius,5.0,.12*s.radius),
                                 vec3(.24*s.radius,68.0,.19*s.radius));
    rock=landUnion(rock,shoulder,16.0);
    rock+=noiseSimplex3D(q*.011,uSeed^707u)*1.7;
    float target=max(base,rock*uDetailScale);
    // A lateral cleft opens in one flank while the top remains rounded.
    float cleft=landEllipsoid(q-vec3(-.39*s.radius,7.0,.06*s.radius),
                             vec3(.24*s.radius,39.0,mix(27.0,49.0,s.variant)));
    target=min(target,cleft*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
