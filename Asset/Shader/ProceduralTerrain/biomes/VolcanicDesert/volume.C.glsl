#version 450 core
// Biome 20: VolcanicDesert. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 20u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2600.0,2010u,.64);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // A raised, lobed lava sheet contains a horizontal tube. Both ends open
    // into the air at the sheet edge, instead of sealing a tube underground.
    float width=s.radius*.44;
    vec3 t=q-vec3(0.0,23.0,-s.radius*.12);
    t.z+=14.0*sin(t.x/105.0+s.variant*5.0);
    float a=-landEllipsoid(t,vec3(width,82.0,s.radius*.20));
    float b=-landEllipsoid(t-vec3(width*.48,-9.0,s.radius*.13),
                           vec3(width*.62,61.0,s.radius*.19));
    float lava=landUnion(a,b,16.0);
    float wrinkle=noiseSimplex3D(t/vec3(120.0,64.0,95.0),uSeed^2011u)*3.0;
    float target=max(base,(lava+wrinkle)*uDetailScale);
    float tube=length(vec2((t.y-6.0)*1.08,t.z*.9))-25.0;
    target=min(target,tube*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
