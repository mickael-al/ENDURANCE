#version 450 core
// Biome 18: DryJungle. World-space XZ is in metres; vertical recipe values
// are multiplied by detailScale. Keep site arguments identical in both stages.
#define FIELD_BIOME_ID 18u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Broad stratified tables with a low ramp sector and open valleys.
vec4 fieldRelief(vec2 p)
{
    LandSite s=landSite(p,2200.0,1801u,0.65);
    float h=22.0*landFbm(p/1900.0,1802u)+5.0*landFbm(p/450.0,1803u);
    if(s.mask>0.0)
    {
        vec2 q=landWarp(s.q,510.0,34.0,1804u)/s.radius;
        float outline=max(abs(q.x)*0.92,abs(q.y)*1.12);
        float plateau=1.0-smoothstep(0.39,0.82,outline);
        float ramp=smoothstep(0.05,0.65,q.x)*(1.0-smoothstep(0.14,0.38,abs(q.y)));
        float stratified=landTerrace(plateau*310.0,48.0,0.58);
        float access=plateau*310.0*(1.0-0.68*smoothstep(0.0,0.72,q.x));
        float top=10.0*landFbm(p/230.0,1805u)*plateau;
        h+=s.mask*(mix(stratified,access,ramp)+top);
    }
    return vec4(h,s.variant>=0.38 ? landBand(s,552.0) : 0.0,0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
