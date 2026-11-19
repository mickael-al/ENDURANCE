#version 450 core
// Biome 24: TropicalMangrove. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 24u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2600.0,2410u,.48);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // Rare coastal shelf: low mineral overhang, only ~13 m high at scale .35.
    // The broad ordinary banks receive no caves or protrusions.
    float lengthwise=s.radius*.44;
    float rock=-landEllipsoid(q-vec3(-lengthwise*.15,5.0,0.0),
                              vec3(lengthwise,38.0,s.radius*.24));
    float target=max(base,rock*uDetailScale);
    vec3 cut=q-vec3(lengthwise*.22,8.0,s.radius*.14);
    float alcove=landEllipsoid(cut,vec3(lengthwise*.55,14.0,s.radius*.22));
    target=min(target,alcove*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
