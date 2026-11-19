#version 450 core
// Biome 15: RockDesert. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 15u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Wind-aligned yardangs: long lee slopes, steep noses and an open stony floor.
// The three-octave ridge field only breaks the fins; it is not the mountain shape.
vec4 fieldRelief(vec2 p)
{
    LandSite s=landSite(p,2100.0,1501u,0.64);
    float h=18.0*landFbm(p/1900.0,1502u)+5.0*landFbm(p/420.0,1503u);
    if(s.mask>0.0)
    {
        vec2 q=landWarp(s.q,420.0,28.0,1504u)/s.radius;
        float along=1.0-smoothstep(0.28,0.86,abs(q.y));
        float nose=mix(0.72,1.0,smoothstep(-0.65,0.18,q.y));
        float finA=1.0-smoothstep(0.018,0.16,abs(q.x+0.20));
        float finB=1.0-smoothstep(0.025,0.20,abs(q.x-0.20));
        float fins=max(finA,finB*0.76)*along*nose;
        float ridge=landRidges(p/145.0,1505u);
        float rock=landTerrace(fins*(330.0+75.0*ridge),26.0,0.72);
        h+=s.mask*rock;
    }
    return vec4(h,s.variant>0.34 ? landBand(s,560.0) : 0.0,0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
