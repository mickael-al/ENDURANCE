// Terrain composition primitives. Independent biomes choose their own shapes.
#ifndef ENDURANCE_BIOME_LANDFORMS_2D_GLSL
#define ENDURANCE_BIOME_LANDFORMS_2D_GLSL
#include "biome_relief_common.glsl"
#include "biome_landforms_common.glsl"

float landFbm(vec2 p,uint salt)
{
    return noiseSimplexFbm2D(p,uSeed^salt,3,2.03,0.48);
}

// Feedback suppresses fine ridges in low regions rather than scattering the
// same roughness over valleys and summits. This is a shape, not erosion.
float landRidges(vec2 p,uint salt)
{
    float sum=0.0,amplitude=0.57,weight=1.0,normalization=0.0;
    for(int octave=0;octave<3;++octave)
    {
        float ridge=1.0-abs(noiseSimplex2D(p,(uSeed^salt)+uint(octave)*0x9e3779b9u));
        ridge*=ridge;
        sum+=ridge*weight*amplitude;
        normalization+=amplitude;
        weight=clamp(ridge*1.8,0.0,1.0);
        p*=2.03;
        amplitude*=0.48;
    }
    return sum/normalization;
}

vec2 landWarp(vec2 p,float wavelength,float amplitude,uint salt)
{
    vec2 q=p/max(wavelength,1.0);
    return p+amplitude*vec2(noiseSimplex2D(q,uSeed^salt),
        noiseSimplex2D(q,uSeed^salt^0x68bc21ebu));
}

float landTerrace(float height,float stepHeight,float softness)
{
    float stepSize=max(stepHeight,0.001);
    float q=height/stepSize;
    float soft=clamp(softness,0.05,1.0);
    float width=min(soft,0.5);
    float terraced=(floor(q)+smoothstep(0.5-width,0.5+width,fract(q)))*stepSize;
    // Above .5 there is no flat interval left. Continue softening toward the
    // original slope instead of silently making all .5..1 settings identical.
    return mix(terraced,height,max(2.0*soft-1.0,0.0));
}

float landBand(LandSite site,float rawThickness)
{
    return clamp(rawThickness,0.0,576.0)*smoothstep(0.0,0.08,site.mask);
}

// A bounded peak with asymmetric ridged buttresses and secondary fBm detail.
// Exact centre height is peakMetres at the scene's reference detailScale=.35;
// changing detailScale deliberately scales the whole biome as before.
float landMountain(LandSite site,float peakMetres,uint salt)
{
    if(site.mask<=0.0) return 0.0;
    float radius=max(site.radius,1.0);
    float radial=length(site.q)/radius;
    float envelope=pow(max(1.0-radial,0.0),1.15);
    vec2 q=landWarp(site.q,radius*.55,radius*.055,salt)
        +site.variant*vec2(173.0,297.0);
    float ridges=landRidges(q/(radius*.43),salt^0x7521u);
    float broad=clamp(.5+.5*landFbm(q/(radius*.8),salt^0x6837u),0.0,1.0);
    float buttress=.48+.38*ridges+.14*broad;
    float profile=envelope*mix(1.0,buttress,smoothstep(0.0,.28,radial));
    return (peakMetres/.35)*profile*site.mask;
}
#endif
