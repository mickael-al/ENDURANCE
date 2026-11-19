#version 450 core
// Biome 22: AridPlateau. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 22u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // Mesa layers depend on broad structure and ridged tributary folds.
    vec2 q=landWarp(p,3400.0,240.0,2201u);
    vec2 axis=noiseRotate2D(q,vec2(.8,-.6));
    float broad=landFbm(q/2100.0,2202u);
    float folds=landRidges(axis/vec2(2400.0,760.0),2203u);
    float mass=smoothstep(-.28,.35,broad+.23*(folds-.5));
    float table=landTerrace(mass*380.0,102.0,.52);
    float spine=1800.0*sin(axis.x/1800.0+.42*landFbm(axis/2500.0,2204u));
    float valley=1.0-smoothstep(90.0,290.0,abs(spine));
    // The very broad crossing saddle breaks the otherwise continuous table.
    float crossing=2400.0*sin(axis.y/2400.0+.32*noiseSimplex2D(q/3100.0,uSeed^2205u));
    float saddle=1.0-smoothstep(65.0,220.0,abs(crossing));
    float low=20.0*landFbm(q/3000.0,2206u);
    float h=mix(table+18.0*folds,low,max(valley,saddle*.82));
    h+=1.3*noiseSimplex2D(q/100.0,uSeed^2207u)*mass;
    LandSite s=landSite(p,2700.0,2210u,.60);
    h+=s.mask*38.0*(1.0-smoothstep(.15,.85,length(s.q)/s.radius));
    return vec4(h,landBand(s,512.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
