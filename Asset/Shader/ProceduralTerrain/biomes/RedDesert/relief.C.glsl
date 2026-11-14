#version 450 core
// Biome 21: RedDesert. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 21u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // Compound dunes: asymmetric long waves, curved phase and smaller windward
    // dunes. Wide low combes remain between them; no fractal rubble layer.
    vec2 q=landWarp(p,3200.0,170.0,2101u);
    vec2 wind=noiseRotate2D(q,vec2(.6,.8));
    float region=landFbm(q/5200.0,2102u);
    float bend=landFbm(q/1700.0,2103u);
    float phase=fract(wind.x/440.0+.38*bend+.10*sin(wind.y/700.0));
    float dune=phase<.72 ? smoothstep(0.0,.72,phase)
                          : 1.0-smoothstep(.72,1.0,phase);
    float compound=noiseDunes2D01(q,vec2(.8,.6),190.0,uSeed^2104u);
    float crest=noiseContrast01(dune,1.18,.48);
    float h=(112.0+34.0*region)*crest+13.0*compound*dune*(1.0-dune)
        +24.0*landFbm(q/3500.0,2105u);
    LandSite s=landSite(p,2900.0,2110u,.55);
    // Flatten the sand beneath the rock site; low sandstone footing blends in.
    h=mix(h,32.0+18.0*region,.68*s.mask);
    h+=s.mask*20.0*(1.0-smoothstep(.1,.74,length(s.q)/s.radius));
    return vec4(h,landBand(s,576.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
