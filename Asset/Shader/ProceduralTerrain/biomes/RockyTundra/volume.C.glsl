#version 450 core
// Biome 5: RockyTundra. Multiscale terrain with sparse, bounded sculptural sites.
// Positive density is solid; return only this biome's signed density delta.
#define FIELD_BIOME_ID 5u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    LandSite s=landSite(p.xz,2100.0,506u,.66);
    LandSite hole=landRkmSite(p.xz,0.25,5112u);
    float mask=max(s.mask,hole.mask);
    if(mask<=0.0 || !landVolumeEnabled(column)) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // An inclined thick slab is added above the heightfield. A second, lower
    // slab changes composition per site; their bases remain buried in ground.
    vec3 a=q-vec3(-.12*s.radius,53.0,0.0);
    a.x-=.72*a.y;
    float rock=-landBox(a,vec3(.29*s.radius,91.0,.105*s.radius));
    if(s.variant>.48)
    {
        vec3 b=q-vec3(.24*s.radius,24.0,.18*s.radius);
        b.x+=.4*b.y;
        rock=landUnion(rock,-landBox(b,vec3(.13*s.radius,54.0,.24*s.radius)),9.0);
    }
    rock+=noiseSimplex3D(q*.016,uSeed^507u)*2.5;
    float target=max(base,rock*uDetailScale);
    // Side-cut at the heel, never a repeated crack across every running slab.
    float alcove=landEllipsoid(q-vec3(-.29*s.radius,12.0,.07*s.radius),
                              vec3(.18*s.radius,33.0,.14*s.radius));
    target=min(target,alcove*uDetailScale);
    target=mix(base,target,s.mask/max(mask,0.000001));
    if(hole.mask>0.0)
    {
        vec3 c=landVolumeLocal(p,column,hole);
        float chamber=landEllipsoid(c-vec3(25.0,-44.0,0.0),vec3(73.0,42.0,49.0));
        float entrance=landCapsule(c,vec3(-75.0,14.0,0.0),vec3(22.0,-42.0,0.0),23.0);
        float excavation=min(chamber,entrance);
        excavation+=noisePerlin3D(c*vec3(.024,.032,.021),uSeed^5113u)*6.0;
        target=mix(target,min(target,excavation*uDetailScale),hole.mask/max(mask,0.000001));
    }
    return landDelta(p,column,target,mask);
}

#include "../../field_volume_accumulate.glsl"
