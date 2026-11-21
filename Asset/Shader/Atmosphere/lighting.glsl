// Material-facing interface: direct source transmission, IBL tint and a
// perspective sample at THIS fragment, shared by deferred and forward paths.
#ifndef GE_ATMOSPHERE_LIGHTING_GLSL
#define GE_ATMOSPHERE_LIGHTING_GLSL
#include "sample.glsl"
#include "../Shadow/directional_surface.glsl"
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
    if(index==uAtmosphereMoonIndex && geAtmMoonRadiance.w>0.5)
        return geAtmMoonRadiance.rgb*geAtmDirectTransmission(world,direction);
    float multiplier=1.0;
    if(index==uAtmosphereSunIndex) multiplier=uAtmosphereSunScale;
    else if(index==uAtmosphereMoonIndex) multiplier=uAtmosphereMoonScale;
    else return radiance;
    return radiance*max(multiplier,0.0)*geAtmDirectTransmission(world,direction);
}
float geAtmosphereDirectSpecular(int index)
{
    // Textured moons already appear in the GGX environment as an area source.
    return uAtmosphereActive && uAtmosphereIblActive && index==uAtmosphereMoonIndex
        && geAtmMoonRadiance.w>0.5 ? 0.0 : 1.0;
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
