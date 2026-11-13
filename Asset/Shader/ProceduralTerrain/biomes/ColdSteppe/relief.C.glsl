#version 450 core
// Biome 6: ColdSteppe. Multiscale terrain with sparse, bounded sculptural sites.
// Relative height / total 3D band / offset / local site support, before detailScale.
#define FIELD_BIOME_ID 6u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q=noiseRotate2D(landWarp(p,4300.0,210.0,601u),vec2(.96,.28));
    // Slow bends and independent octaves break the periodicity of the broad
    // asymmetric swells. Fine relief stays far below a 1.8 m player's height.
    float phase=q.x*.0042+.65*noiseSimplex2D(q*.00043,uSeed^602u);
    float swell=.5+.5*sin(phase);
    float h=82.0*(swell*swell-.35);
    h+=landFbm(q*vec2(.00033,.00012),603u)*93.0;
    h+=landFbm(q*vec2(.0012,.00048),604u)*15.0;
    h+=noiseSimplex2D(q*.009,uSeed^605u)*.9;
    LandSite s=landSite(p,2700.0,606u,.52);
    vec2 t=s.q/s.radius;
    float ramp=smoothstep(-.62,.56,t.x)*(1.0-smoothstep(.30,.86,abs(t.y)));
    h+=s.mask*ramp*(110.0+70.0*s.variant);
    return vec4(h,landBand(s,360.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
