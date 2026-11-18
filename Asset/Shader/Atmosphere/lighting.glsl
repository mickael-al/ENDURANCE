// Material-facing interface: direct source transmission, IBL tint and a
// perspective sample at THIS fragment, shared by deferred and forward paths.
#ifndef GE_ATMOSPHERE_LIGHTING_GLSL
#define GE_ATMOSPHERE_LIGHTING_GLSL
#include "sample.glsl"
uniform bool uAtmosphereIblActive;
uniform float uAtmosphereIblSaturation;
uniform int uAtmosphereSunIndex;
uniform int uAtmosphereMoonIndex;
uniform float uAtmosphereSunScale;
uniform float uAtmosphereMoonScale;
uniform vec2 uAtmosphereInvResolution;
vec3 geAtmosphereDirect(vec3 radiance,int index,vec3 world,vec3 direction)
{
    if(!uAtmosphereActive) return radiance;
    float multiplier=1.0;
    if(index==uAtmosphereSunIndex) multiplier=uAtmosphereSunScale;
    else if(index==uAtmosphereMoonIndex) multiplier=uAtmosphereMoonScale;
    else return radiance;
    return radiance*max(multiplier,0.0)*geAtmDirectTransmission(world,direction);
}
float geAtmosphereDirectionalSurfaceBias(mat4 projectionView,vec3 normal,vec3 lightDirection,float resolution)
{
    float cosine=clamp(dot(normal,lightDirection),0.0,1.0);
    float slope=min(sqrt(max(1.0-cosine*cosine,0.0))/max(cosine,0.1),4.0);
    // Bias is measured in receiver texels and world metres. Extending caster
    // depth must never turn a near-camera bias into tens of metres.
    float inverseWidth=length(vec3(projectionView[0][0],projectionView[1][0],projectionView[2][0]));
    float worldTexel=2.0/max(inverseWidth*max(resolution,1.0),1e-8);
    return geAtmDirectionalShadowBias(projectionView,0.025+worldTexel*(0.15+0.35*slope));
}
float geAtmosphereShadow(int index,float visibility,float legacyAmbient)
{
    if(uAtmosphereIblActive && (index==uAtmosphereSunIndex || index==uAtmosphereMoonIndex))
        return visibility;
    return mix(legacyAmbient,1.0,visibility);
}
vec3 geAtmosphereIbl(vec3 radiance)
{
    return uAtmosphereIblActive?geAtmSaturate(radiance,uAtmosphereIblSaturation):radiance;
}
#endif
