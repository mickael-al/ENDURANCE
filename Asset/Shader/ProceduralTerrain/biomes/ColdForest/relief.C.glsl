#version 450 core
// Biome 8: ColdForest. Multiscale terrain with sparse, bounded sculptural sites.
// Relative height / total 3D band / offset / local site support, before detailScale.
#define FIELD_BIOME_ID 8u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 q=noiseRotate2D(landWarp(p,3500.0,260.0,801u),vec2(.6,.8));
    // Explicit broad U floors with independently sculpted sidewalls. Ridges
    // contribute mainly above the floor, retaining continuous running space.
    float axis=q.x*.0021+.7*noiseSimplex2D(q*vec2(.00021,.00065),uSeed^802u);
    float wall=smoothstep(.18,.78,abs(sin(axis)));
    float ridges=landRidges(q*vec2(.0009,.0015),803u);
    float region=.5+.5*noiseSimplex2D(q*.00016,uSeed^804u);
    float h=-108.0+wall*(240.0+160.0*region+95.0*ridges*ridges);
    h+=landFbm(q*vec2(.00048,.00024),805u)*24.0;
    h+=noiseSimplex2D(q*.007,uSeed^806u)*1.8*wall;
    LandSite s=landSite(p,2400.0,807u,.63);
    vec2 t=s.q/s.radius;
    // Crescent headwall with a broad mouth; no closed circular cliff trap.
    float ring=exp(-18.0*(length(t)-.52)*(length(t)-.52));
    float mouth=smoothstep(-.38,.04,t.x);
    h+=s.mask*ring*mouth*(95.0+70.0*s.variant);
    return vec4(h,landBand(s,576.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
