#ifndef GE_ATMOSPHERE_PHYSICAL_GLSL
#define GE_ATMOSPHERE_PHYSICAL_GLSL
#include "atmosphere_common.glsl"

#ifndef GE_ATM_TRANSMITTANCE_BINDING
#define GE_ATM_TRANSMITTANCE_BINDING 0
#endif
#ifndef GE_ATM_MULTISCATTER_BINDING
#define GE_ATM_MULTISCATTER_BINDING 1
#endif
layout(binding=GE_ATM_TRANSMITTANCE_BINDING) uniform sampler2D geAtmTransmittanceLut;
layout(binding=GE_ATM_MULTISCATTER_BINDING) uniform sampler2D geAtmMultiscatterLut;

float geAtmHorizonCosine(float radius)
{
    float r=max(radius,geAtmRadii.x+0.1);
    return -sqrt(max(1.0-(geAtmRadii.x/r)*(geAtmRadii.x/r),0.0));
}

vec2 geAtmTransmittanceUV(float radius,float mu)
{
    float horizon=geAtmHorizonCosine(radius);
    return vec2(sqrt(clamp((mu-horizon)/(1.0-horizon),0.0,1.0)),
        sqrt(clamp((radius-geAtmRadii.x)/max(geAtmRadii.y-geAtmRadii.x,1.0),0.0,1.0)));
}

vec3 geAtmSunTransmittance(vec3 planetPosition,vec3 sourceDirection)
{
    if(!GE_ATM_PHYSICAL_ON) return vec3(1.0);
    float radius=length(planetPosition);
    if(radius>geAtmRadii.y)
    {
        vec2 intersection=geAtmSphere(planetPosition,sourceDirection,geAtmRadii.y);
        if(intersection.x<=0.0) return vec3(1.0);
        planetPosition+=sourceDirection*intersection.x;
        radius=length(planetPosition);
    }
    float mu=dot(planetPosition,sourceDirection)/max(radius,1.0);
    // Reject planet-occluded paths analytically; LUT interpolation must never
    // leak sunlight across the ground intersection discontinuity.
    if(mu<geAtmHorizonCosine(radius)) return vec3(0.0);
    return clamp(textureLod(geAtmTransmittanceLut,geAtmTransmittanceUV(radius,mu),0.0).rgb,vec3(0.0),vec3(1.0));
}

vec3 geAtmMultipleScattering(vec3 planetPosition,vec3 sourceDirection)
{
    float radius=length(planetPosition);
    vec2 uv=vec2(0.5+0.5*dot(planetPosition,sourceDirection)/max(radius,1.0),
        sqrt(clamp((radius-geAtmRadii.x)/max(geAtmRadii.y-geAtmRadii.x,1.0),0.0,1.0)));
    return max(textureLod(geAtmMultiscatterLut,uv,0.0).rgb,vec3(0.0));
}

vec3 geAtmSourceForLight(vec3 position,vec3 direction,vec3 sourceDirection,
    vec3 irradiance,float visibility,vec3 rayleigh,vec3 mie)
{
    float mu=dot(direction,sourceDirection);
    vec3 single=geAtmSunTransmittance(position,sourceDirection)
        *(rayleigh*geAtmRayleighPhase(mu)+mie*geAtmMiePhase(mu,geAtmMedium.y))*clamp(visibility,0.0,1.0);
    vec3 multiple=geAtmMultipleScattering(position,sourceDirection)*(rayleigh+mie)*max(geAtmMedium.w,0.0);
    return max(irradiance,vec3(0.0))*(single+multiple);
}

vec3 geAtmPhysicalSource(vec3 position,vec3 direction,vec2 sourceVisibility,out vec3 extinction)
{
    if(!GE_ATM_PHYSICAL_ON) { extinction=vec3(0.0); return vec3(0.0); }
    vec3 r,m;
    geAtmMediumAt(position,r,m,extinction);
    vec3 source=vec3(0.0);
    if(geAtmSunLightingEnabled())
        source+=geAtmSourceForLight(position,direction,normalize(geAtmSunDirection.xyz),
            geAtmSunRadiance.rgb,sourceVisibility.x,r,m);
    if(geAtmMoonLightingEnabled())
        source+=geAtmSourceForLight(position,direction,normalize(geAtmMoonDirection.xyz),
            geAtmMoonRadiance.rgb,sourceVisibility.y,r,m);
    return geAtmArtistic(source,direction);
}

vec3 geAtmPhysicalSky(vec3 direction)
{
    if(!GE_ATM_PHYSICAL_ON)
    {
        vec3 fallback=max(geAtmPalette(direction)*geAtmArt.x,vec3(0.0));
#ifdef GE_CUSTOM_RADIANCE
        fallback=max(geCustomizeRadiance(fallback,direction),vec3(0.0));
#endif
        return fallback;
    }
    vec3 origin=geAtmOrigin();
    vec2 path=geAtmPath(origin,direction);
    if(path.y<=path.x) return vec3(0.0);
    vec3 scattering=vec3(0.0),transmittance=vec3(1.0);
    int samples=clamp(geAtmQuality.x,4,256);
    for(int i=0;i<samples;++i)
    {
        // Quadratic spacing keeps the near-camera exponential layers smooth.
        float a=float(i)/float(samples),b=float(i+1)/float(samples);
        float begin=mix(path.x,path.y,a*a),end=mix(path.x,path.y,b*b);
        vec3 position=origin+direction*(0.5*(begin+end));
        vec3 extinction;
        vec3 source=geAtmPhysicalSource(position,direction,vec2(1.0),extinction);
        geAtmIntegrate(scattering,transmittance,source,extinction,end-begin);
    }
    // Lambertian planetary ground, useful for the lower IBL hemisphere.
    vec3 endpoint=origin+direction*path.y;
    if(length(endpoint)<geAtmRadii.x+10.0)
    {
        vec3 up=normalize(endpoint),ground=vec3(0.0);
        if(geAtmSunLightingEnabled())
            ground+=geAtmSunRadiance.rgb*geAtmSunTransmittance(endpoint+up,normalize(geAtmSunDirection.xyz))
                *max(dot(up,normalize(geAtmSunDirection.xyz)),0.0);
        if(geAtmMoonLightingEnabled())
            ground+=geAtmMoonRadiance.rgb*geAtmSunTransmittance(endpoint+up,normalize(geAtmMoonDirection.xyz))
                *max(dot(up,normalize(geAtmMoonDirection.xyz)),0.0);
        scattering+=transmittance*geAtmArtistic(ground*clamp(geAtmArt.w,0.0,1.0)/GE_ATM_PI,direction);
    }
    return max(scattering,vec3(0.0));
}

#endif
