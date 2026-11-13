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
    // Broad tiered peaks: feedback ridges/fBm shape the massif; discrete strata
    // give this steppe a different silhouette from the smooth tundra peaks.
    LandSite mountain=landRkmSite(p,0.5,611u);
    float massif=landMountain(mountain,620.0,612u);
    float strata=0.78*smoothstep(0.0,0.20,length(mountain.q)/mountain.radius);
    h+=mix(massif,landTerrace(massif,70.0/0.35,0.12),strata);

    // A finite, meandering 800 m chasm. Narrow edge transitions retain an
    // abrupt wall; the tapered ends and compact support keep density continuous.
    LandSite rift=landRkmSite(p,0.25,613u);
    float along=1.0-smoothstep(rift.radius*0.58,rift.radius*0.82,abs(rift.q.x));
    float bend=38.0*sin(rift.q.x/max(rift.radius*0.26,1.0));
    bend+=12.0*noiseSimplex2D(rift.q/240.0,uSeed^614u);
    float halfWidth=mix(75.0,125.0,rift.variant);
    float trench=1.0-smoothstep(halfWidth-10.0,halfWidth,abs(rift.q.y-bend));
    h-=rift.mask*along*trench*(800.0/0.35);
    return vec4(h,landBand(s,360.0),0.0,s.mask);
}

#include "../../field_surface_accumulate.glsl"
