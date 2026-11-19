#version 450 core
// Biome 19: TropicalJungle. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 19u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Karst grows in sparse, unequal groups above a continuous basin floor.
vec4 fieldRelief(vec2 p)
{
    // Smaller karst groups fit the fragmented warm/wet climate regions.
    // At 2400 m the reference world missed every dominant jungle region.
    LandSite s=landSite(p,1600.0,1901u,0.68);
    float h=18.0*landFbm(p/1800.0,1902u)+5.0*landFbm(p/420.0,1903u);
    if(s.mask>0.0)
    {
        vec2 q=landWarp(s.q,370.0,25.0,1904u)/s.radius;
        float a=1.0-smoothstep(0.09,0.34,length(q-vec2(-0.25,-0.12)));
        float b=1.0-smoothstep(0.08,0.31,length((q-vec2(0.24,-0.03))*vec2(1.0,0.86)));
        float c=1.0-smoothstep(0.06,0.25,length(q-vec2(0.02,0.36)));
        float towers=max(a*470.0,max(b*375.0,c*295.0));
        float pedestal=50.0*max(0.0,1.0-dot(q,q)/0.72);
        float ridges=landRidges(p/135.0,1905u);
        h+=s.mask*(towers*(0.90+0.10*ridges)+pedestal);
    }
    return vec4(h,s.variant>=0.34 ? landBand(s,568.0) : 0.0,0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
