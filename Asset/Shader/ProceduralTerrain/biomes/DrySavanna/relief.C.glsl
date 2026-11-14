#version 450 core
// Biome 16: DrySavanna. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 16u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Wide rolling plain interrupted by compact, unequal granite kopjes.
vec4 fieldRelief(vec2 p)
{
    LandSite s=landSite(p,1800.0,1601u,0.60);
    float h=22.0*landFbm(p/1700.0,1602u)+5.0*landFbm(p/390.0,1603u);
    if(s.mask>0.0)
    {
        vec2 q=landWarp(s.q,390.0,18.0,1604u)/s.radius;
        float a=max(0.0,1.0-length((q-vec2(-0.22,-0.10))/vec2(0.33,0.30)));
        float b=max(0.0,1.0-length((q-vec2(0.22,0.08))/vec2(0.25,0.34)));
        float c=max(0.0,1.0-length((q-vec2(0.02,0.36))/vec2(0.23,0.22)));
        // Rounded dome profiles retain ramps around their feet.
        a=a*a*(3.0-2.0*a); b=b*b*(3.0-2.0*b); c=c*c*(3.0-2.0*c);
        float group=max(a*255.0,max(b*185.0,c*125.0));
        float shoulder=44.0*max(0.0,1.0-dot(q,q)/0.70);
        h+=s.mask*(group+shoulder+9.0*landRidges(p/105.0,1605u)*max(a,max(b,c)));
    }
    return vec4(h,s.variant>0.40 ? landBand(s,552.0) : 0.0,0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
