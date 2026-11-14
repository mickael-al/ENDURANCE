#version 450 core
// Biome 7: BorealForest. Multiscale terrain with sparse, bounded sculptural sites.
// Relative height / total 3D band / offset / local site support, before detailScale.
#define FIELD_BIOME_ID 7u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q=landWarp(p,2800.0,210.0,701u);
    float rounded=.5+.5*landFbm(q*vec2(.00065,.00039),702u);
    float regional=.5+.5*noiseSimplex2D(q*.00019,uSeed^703u);
    float h=rounded*rounded*(210.0+110.0*regional)-72.0;
    h+=landFbm(q*vec2(.0015,.00086),704u)*24.0;
    LandSite s=landSite(p,1850.0,705u,.70);
    vec2 t=s.q/(s.radius*vec2(.85,.59));
    float dome=1.0-smoothstep(.04,1.0,dot(t,t));
    h+=s.mask*dome*(190.0+100.0*s.variant);
    h+=noiseSimplex2D(q*.008,uSeed^706u)*1.4;
    return vec4(h,landBand(s,512.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
