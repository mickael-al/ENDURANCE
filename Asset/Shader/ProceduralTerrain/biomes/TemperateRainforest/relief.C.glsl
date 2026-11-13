#version 450 core
// Biome 13: TemperateRainforest. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 13u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Heavy walls beside broad connected valley floors; region-dependent benches.
vec4 fieldRelief(vec2 p)
{
    vec2 q=noiseRotate2D(landWarp(p,4200.0,340.0,13101u),vec2(.8,-.6));
    float regional=landFbm(q/3600.0,13102u);
    float phase=q.x/850.0+.72*landFbm(q/1900.0,13103u);
    float cross=abs(sin(phase));
    float wall=smoothstep(.18,.75,cross);
    float shoulders=landRidges(q*vec2(.0008,.0004),13104u);
    float h=235.0*wall+65.0*regional+40.0*shoulders*wall;
    float terraced=landTerrace(h,80.0,.76);
    h=mix(h,terraced,.40*smoothstep(.25,.8,wall));
    h+=2.0*noiseSimplex2D(q/145.0,uSeed^13105u)*wall;
    LandSite s=landSite(p,2200.0,13110u,.72);
    vec2 u=s.q/s.radius;
    float rim=(1.0-smoothstep(.13,.32,abs(length(u)-.55)))*smoothstep(-.2,.25,-u.x);
    h+=s.mask*rim*(100.0+35.0*s.variant);
    return vec4(h,landBand(s,576.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
