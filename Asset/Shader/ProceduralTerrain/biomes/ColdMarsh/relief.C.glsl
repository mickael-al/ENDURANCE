#version 450 core
// Biome 9: ColdMarsh. Multiscale terrain with sparse, bounded sculptural sites.
// Relative height / total 3D band / offset / local site support, before detailScale.
#define FIELD_BIOME_ID 9u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q=landWarp(p,3200.0,180.0,901u);
    float broad=.5+.5*landFbm(q*.00062,902u);
    // Redistribution flattens the basin floors; only broad shore levels rise.
    float banks=smoothstep(.42,.78,broad);
    float h=-28.0+landTerrace(banks*68.0,17.0,.5);
    h+=landFbm(q*.00020,903u)*17.0;
    h+=noiseSimplex2D(q*.006,uSeed^904u)*.65*(.2+.8*banks);
    LandSite s=landSite(p,2300.0,905u,.58);
    vec2 t=s.q/s.radius;
    float radius=length(t);
    float shore=exp(-35.0*(radius-.52)*(radius-.52));
    // Break the ring in two broad directions rather than trapping the player.
    float openings=smoothstep(.10,.34,abs(t.y));
    h+=s.mask*shore*openings*(54.0+45.0*s.variant);
    return vec4(h,landBand(s,256.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
