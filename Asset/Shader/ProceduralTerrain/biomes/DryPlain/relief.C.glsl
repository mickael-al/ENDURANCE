#version 450 core
// Biome 10: DryPlain. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 10u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Broad alluvial ground; dry channels remain shallow and widely separated.
vec4 fieldRelief(vec2 p)
{
    vec2 q=landWarp(p,4600.0,170.0,10101u);
    q=noiseRotate2D(q,vec2(.8,.6));
    float regional=landFbm(q/4800.0,10102u);
    float phase=q.x/760.0+.65*noiseSimplex2D(q/1900.0,uSeed^10103u);
    float distanceToChannel=abs(sin(phase));
    float channel=1.0-smoothstep(.035,.16,distanceToChannel);
    float bank=smoothstep(.08,.17,distanceToChannel)*(1.0-smoothstep(.20,.33,distanceToChannel));
    float h=28.0*regional+18.0*landFbm(q/1250.0,10104u);
    h+=5.0*bank-12.0*channel;
    h+=.65*noiseSimplex2D(q/95.0,uSeed^10105u)*(1.0-channel);
    LandSite s=landSite(p,2600.0,10110u,.60);
    float outcrop=1.0-smoothstep(.10,.85,length(s.q/vec2(s.radius,s.radius*.42)));
    h+=s.mask*outcrop*(65.0+35.0*s.variant);
    return vec4(h,landBand(s,384.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
