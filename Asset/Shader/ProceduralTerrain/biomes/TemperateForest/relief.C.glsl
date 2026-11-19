#version 450 core
// Biome 12: TemperateForest. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 12u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Cuestas: long dip slopes and shorter scarps, with open crossing saddles.
vec4 fieldRelief(vec2 p)
{
    vec2 q=noiseRotate2D(landWarp(p,3900.0,240.0,12101u),vec2(.6,.8));
    float regional=landFbm(q/3100.0,12102u);
    float phase=fract(q.x/1650.0+.16*noiseSimplex2D(q/1800.0,uSeed^12103u));
    float cuesta=smoothstep(0.0,.72,phase)*(1.0-smoothstep(.79,1.0,phase));
    float saddle=1.0-.72*smoothstep(.20,.72,noiseSimplex2D(q/1700.0,uSeed^12104u));
    float raw=145.0*cuesta*saddle+85.0*regional;
    float ramp=1.0-smoothstep(.12,.32,abs(noiseSimplex2D(q/2100.0,uSeed^12105u)));
    float h=mix(landTerrace(raw,54.0,.62),raw,ramp*.85);
    h+=3.0*noiseSimplex2D(q/170.0,uSeed^12106u);
    LandSite s=landSite(p,2050.0,12110u,.67);
    float ledge=1.0-smoothstep(.15,.80,length(s.q/vec2(.8*s.radius,.46*s.radius)));
    h+=s.mask*ledge*(90.0+40.0*s.variant);
    return vec4(h,landBand(s,512.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
