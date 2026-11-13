#version 450 core
// Biome 13: TemperateRainforest. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 13u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

// A massive open alcove: broad cap on offset buttresses, not random cave spam.
float fieldVolume(vec3 p,FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2200.0,13110u,.72);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    float warp=3.0*noiseSimplex3D(q*.010,uSeed^13111u);
    vec3 bent=q-vec3(-.10*q.y,warp,0.0);
    // Low, broad amphitheatres and higher offset awnings share no fixed silhouette.
    float rise=mix(90.0,136.0,s.variant);
    float cap=-landEllipsoid(bent-vec3(-.10*s.radius,rise,0.0),
        vec3(mix(.53,.40,s.variant)*s.radius,50.0,.31*s.radius));
    float back=-landEllipsoid(bent-vec3(-.30*s.radius,32.0,0.0),vec3(.19*s.radius,rise,.36*s.radius));
    float shoulder=-landEllipsoid(bent-vec3(.10*s.radius,22.0,-.25*s.radius),
        vec3(.22*s.radius,mix(110.0,76.0,s.variant),.18*s.radius));
    float rock=landUnion(landUnion(cap,back,18.0),shoulder,13.0);
    float target=max(base,rock*uDetailScale);
    float cave=landEllipsoid(q-vec3(.08*s.radius,36.0,.13*s.radius),vec3(.28*s.radius,51.0,.30*s.radius));
    target=min(target,cave*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
