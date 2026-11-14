#version 450 core
// Biome 22: AridPlateau. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 22u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2700.0,2210u,.60);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    float reach=s.radius*.42;
    // A tapered buttress carries a thick projecting layer. Side excavation
    // makes an alcove; the mesa top is not perforated by a random hole field.
    float foot=-landEllipsoid(q-vec3(-reach*.47,38.0,0.0),vec3(reach*.43,103.0,s.radius*.23));
    vec3 slab=q-vec3(18.0,104.0,0.0);
    slab.y+=.055*slab.x;
    float cap=-landBox(slab,vec3(reach,28.0,s.radius*.26));
    float rock=landUnion(foot,cap,12.0);
    float target=max(base,rock*uDetailScale);
    vec3 alcove=q-vec3(reach*.35,36.0,0.0);
    float cut=landEllipsoid(alcove,vec3(reach*.65,45.0,s.radius*.30));
    target=min(target,cut*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
