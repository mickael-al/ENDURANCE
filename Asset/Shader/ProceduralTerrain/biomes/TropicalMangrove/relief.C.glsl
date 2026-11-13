#version 450 core
// Biome 24: TropicalMangrove. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 24u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // A hierarchy of wide shallow distributaries and low banks. Cross-channel
    // depth stays small relative to a 1.8 m character; no water is required.
    vec2 q=landWarp(p,2800.0,130.0,2401u);
    vec2 tide=noiseRotate2D(q,vec2(.8,.6));
    float meander=landFbm(q/1900.0,2402u);
    float channel=abs(sin(tide.x/520.0+.8*meander));
    float branches=abs(sin(tide.x/310.0+.45*tide.y/680.0+.55*meander));
    float branchZone=smoothstep(.05,.45,noiseSimplex2D(q/3200.0,uSeed^2403u));
    float d=min(channel,mix(1.0,branches,branchZone));
    float bank=smoothstep(.10,.44,d);
    float shelf=smoothstep(.22,.68,d);
    float h=9.0*bank+4.0*shelf+5.0*landFbm(q/3500.0,2404u);
    h+=.6*noiseSimplex2D(tide/130.0,uSeed^2405u)*bank;
    LandSite s=landSite(p,2600.0,2410u,.48);
    vec2 fan=s.q/s.radius;
    float terrace=(1.0-smoothstep(.12,.84,length(fan*vec2(.8,1.2))))
        *smoothstep(-.85,-.2,fan.x);
    h+=s.mask*28.0*terrace;
    return vec4(h,landBand(s,192.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
