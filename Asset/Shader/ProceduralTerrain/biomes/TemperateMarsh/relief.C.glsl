#version 450 core
// Biome 14: TemperateMarsh. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 14u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Wide meanders, low levees and quiet floodplain; no cellular hole carpet.
vec4 fieldRelief(vec2 p)
{
    vec2 q=noiseRotate2D(landWarp(p,5200.0,170.0,14101u),vec2(.6,-.8));
    float bend=.65*sin(q.y/950.0)+.45*noiseSimplex2D(q/2200.0,uSeed^14102u);
    float d=abs(sin(q.x/1100.0+bend));
    float channel=1.0-smoothstep(.025,.14,d);
    float levee=smoothstep(.08,.17,d)*(1.0-smoothstep(.22,.34,d));
    float h=22.0*landFbm(q/3600.0,14103u)+9.0*landFbm(q/1050.0,14104u);
    h+=11.0*levee-15.0*channel;
    h+=.4*noiseSimplex2D(q/110.0,uSeed^14105u)*(1.0-channel);
    LandSite s=landSite(p,2500.0,14110u,.58);
    vec2 u=s.q/s.radius;
    float crescent=(1.0-smoothstep(.06,.23,abs(length(u*vec2(1.0,mix(.76,1.20,s.variant)))-.52)))*smoothstep(-.22,.28,-u.x);
    h+=s.mask*crescent*(56.0+27.0*s.variant);
    return vec4(h,landBand(s,320.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
