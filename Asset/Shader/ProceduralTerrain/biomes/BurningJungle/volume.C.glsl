#version 450 core
// Biome 23: BurningJungle. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 23u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2500.0,2310u,.67);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    // Rotation varies with height, creating mineral buttresses whose shapes
    // cannot be expressed by a height map. Ground outside the site stays calm.
    float angle=.006*q.y+.8*s.variant;
    vec2 turn=vec2(cos(angle),sin(angle));
    vec2 xz=vec2(dot(q.xz,turn),dot(q.xz,vec2(-turn.y,turn.x)));
    float arm=s.radius*.14;
    vec3 a=vec3(xz.x-arm*.45-.23*q.y,q.y-62.0,xz.y);
    vec3 b=vec3(xz.x+arm*.55+.15*q.y,q.y-48.0,xz.y-arm*.25);
    float first=-landEllipsoid(a,vec3(arm*.74,156.0,arm*.62));
    float second=-landEllipsoid(b,vec3(arm*.65,128.0,arm*.58));
    float rock=landUnion(first,second,22.0);
    float ribs=3.0*noiseSimplex3D(vec3(xz.x,q.y*.6,xz.y)/95.0,uSeed^2311u);
    float target=max(base,(rock+ribs)*uDetailScale);
    float gallery=landEllipsoid(q-vec3(0.0,38.0,0.0),vec3(arm*.45,44.0,arm*1.8));
    target=min(target,gallery*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
