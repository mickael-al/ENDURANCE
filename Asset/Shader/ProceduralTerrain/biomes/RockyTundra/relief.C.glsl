#version 450 core
// Biome 5: RockyTundra. Multiscale terrain with sparse, bounded sculptural sites.
// Relative height / total 3D band / offset / local site support, before detailScale.
#define FIELD_BIOME_ID 5u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

vec4 fieldRelief(vec2 p)
{
    // Folded ridges are redistributed before adding broad slab ramps: neither
    // a single noise nor a field of scattered boulders defines the mountain.
    vec2 q=noiseRotate2D(landWarp(p,2600.0,190.0,501u),vec2(.8,.6));
    float region=smoothstep(-.35,.45,landFbm(q*.00023,502u));
    float ridge=landRidges(q*vec2(.00085,.00145),503u);
    float mountain=ridge*ridge*(.35+.65*region);
    float h=landTerrace(mountain*350.0,44.0,.5)-55.0;
    h+=landFbm(q*vec2(.00036,.00019),504u)*70.0;
    h+=noiseSimplex2D(q*.006,uSeed^505u)*3.0*mountain;
    LandSite s=landSite(p,2100.0,506u,.66);
    vec2 t=s.q/s.radius;
    float slab=clamp(.62+.34*t.x-abs(t.y)*.57,0.0,1.0);
    h+=s.mask*slab*(145.0+90.0*s.variant);
    return vec4(h,landBand(s,576.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
