#version 450 core
// Biome 21: RedDesert. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 21u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_3d.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    if(!landVolumeEnabled(column)) return 0.0;
    LandSite s=landSite(p.xz,2900.0,2110u,.55);
    if(s.mask<=0.0) return 0.0;
    vec3 q=landVolumeLocal(p,column,s);
    float base=column.terrain.x-p.y;
    float width=s.radius*.40;
    // Wide sandstone span. The through-opening has >20 m clearance at the
    // default scale, while the feet stay thick enough for distant meshes.
    vec3 bent=q-vec3(.18*q.y,77.0,0.0);
    float arch=-landEllipsoid(bent,vec3(width,153.0,s.radius*.15));
    float ledge=-landBox(q-vec3(-width*.55,34.0,0.0),vec3(width*.27,56.0,s.radius*.19));
    float rock=landUnion(arch,ledge,14.0);
    float strata=2.8*sin(q.y*.085+noiseSimplex3D(q/150.0,uSeed^2111u));
    float target=max(base,(rock+strata)*uDetailScale);
    // Two regional compositions: an open arch or a smaller high window.
    float highWindow=step(.70,s.variant);
    vec3 aperture=q-vec3(-width*.08,mix(80.0,110.0,highWindow),0.0);
    float hollow=landEllipsoid(aperture,
        vec3(width*mix(.62,.44,highWindow),mix(85.0,52.0,highWindow),s.radius*.34));
    target=min(target,hollow*uDetailScale);
    return landDelta(p,column,target,s.mask);
}

#include "../../field_volume_accumulate.glsl"
