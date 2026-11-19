#version 450 core
// Biome 11: Meadow. Terrain-only, world-space and deterministic.
#define FIELD_BIOME_ID 11u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

// Rounded hills and connected saddles, not high-frequency obstacle noise.
vec4 fieldRelief(vec2 p)
{
    vec2 q=landWarp(p,3300.0,210.0,11101u);
    float broad=landFbm(q/2600.0,11102u);
    float roll=noiseSimplex2D(q/740.0,uSeed^11103u);
    float rounded=roll*(1.0-.35*abs(roll));
    float h=118.0*broad+72.0*rounded;
    h+=7.0*noiseSimplex2D(q/320.0,uSeed^11104u);
    LandSite s=landSite(p,2400.0,11110u,.55);
    vec2 u=s.q/s.radius;
    float ring=1.0-smoothstep(.10,.30,abs(length(u*vec2(mix(.82,1.18,s.variant),1.0))-.58));
    float opening=smoothstep(-.18,.35,-u.x);
    h+=s.mask*ring*opening*(105.0+40.0*s.variant);
    return vec4(h,landBand(s,384.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
