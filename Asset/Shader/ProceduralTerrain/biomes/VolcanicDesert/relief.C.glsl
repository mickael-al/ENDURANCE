#version 450 core
// Biome 20: VolcanicDesert. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 20u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // Sheet-fed lobes and broad shields; the caldera is a regional event.
    vec2 q=landWarp(p,2700.0,210.0,2001u);
    vec2 flow=noiseRotate2D(q,vec2(.8,.6));
    float regional=landFbm(q/4100.0,2002u);
    float tongues=landRidges(flow/vec2(1500.0,430.0),2003u);
    float sheets=landTerrace(56.0*landFbm(flow/1700.0,2004u),26.0,.82);
    float h=34.0*regional+sheets+30.0*tongues;
    h+=2.0*noiseSimplex2D(flow/92.0,uSeed^2005u)*(1.0-.6*tongues);
    LandSite s=landSite(p,2600.0,2010u,.64);
    if(s.mask>0.0)
    {
        vec2 v=s.q/s.radius;
        float radius=length(v*vec2(1.0,.83));
        float shield=1.0-smoothstep(.04,.98,radius);
        float rim=smoothstep(.17,.36,radius)*(1.0-smoothstep(.53,.88,radius));
        // One broad notch leaves a caldera rather than an enclosing wall.
        float breach=1.0-smoothstep(.08,.32,abs(v.y+.12*v.x));
        breach*=smoothstep(-.08,.23,v.x);
        float ring=rim*(1.0-.9*breach);
        float shape=mix(shield*shield,ring,smoothstep(.36,.7,s.variant));
        h+=s.mask*(320.0*shape+24.0*tongues*shield);
    }
    return vec4(h,landBand(s,448.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
