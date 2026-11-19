#version 450 core
// Biome 23: BurningJungle. Terrain-only multiscale recipe, November 2026.
#define FIELD_BIOME_ID 23u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // Ridged multifractal structure multiplied by a slow regional mask leaves
    // open basins. Twisted flow lines are secondary, not a noise blanket.
    vec2 q=landWarp(p,3800.0,290.0,2301u);
    float regional=landFbm(q/4400.0,2302u);
    float massif=smoothstep(-.08,.40,regional);
    vec2 bend=q+vec2(180.0*noiseSimplex2D(q/1900.0,uSeed^2303u),
                    120.0*noiseSimplex2D(q/2200.0,uSeed^2304u));
    float ridge=landRidges(bend/vec2(1400.0,2100.0),2305u);
    float shoulders=landFbm(bend/900.0,2306u)*.5+.5;
    float h=24.0*landFbm(q/2800.0,2307u)
        +massif*(270.0*ridge*ridge+62.0*shoulders*ridge);
    h+=massif*2.0*noiseSimplex2D(bend/110.0,uSeed^2308u);
    LandSite s=landSite(p,2500.0,2310u,.67);
    h+=s.mask*44.0*(1.0-smoothstep(.12,.85,length(s.q)/s.radius));
    return vec4(h,landBand(s,576.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
