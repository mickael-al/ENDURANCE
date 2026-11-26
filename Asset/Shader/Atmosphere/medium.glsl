#ifndef GE_ATMOSPHERE_MEDIUM_GLSL
#define GE_ATMOSPHERE_MEDIUM_GLSL
#include "physical.glsl"
#include "cloud_common.glsl"
#include "shadows.glsl"

#ifdef GE_GAME_FOG
vec4 geGameFog(vec3 worldPosition);
#endif

vec4 geAtmLocalFog(vec3 worldPosition)
{
    if(!GE_ATM_LOCAL_FOG_ON) return vec4(0.0);
    float density=max(geAtmVolume.z,0.0)*exp(-max(worldPosition.y-geAtmVolume.w,0.0)/max(geAtmFog.w,1.0));
    vec4 medium=vec4(clamp(geAtmFog.rgb,vec3(0.0),vec3(1.0))*density,density);
#ifdef GE_GAME_FOG
    // The generic scene fog is an optional additional medium. ENDURANCE's
    // world-space hook may vary independently and never silently hides it.
    vec4 gameMedium=geGameFog(worldPosition);
    gameMedium.a=max(gameMedium.a,0.0);
    gameMedium.rgb=clamp(gameMedium.rgb,vec3(0.0),vec3(gameMedium.a));
    medium+=gameMedium;
#endif
    return medium;
}

vec2 geAtmVisibility(vec3 worldPosition)
{
    vec2 visibility=vec2(1.0);
    if(geAtmSunLightingEnabled())
        visibility.x=geAtmTerrainVisibility(worldPosition,geAtmShadow.x)
            *geAtmCloudVisibility(worldPosition,normalize(geAtmSunDirection.xyz),false);
    if(geAtmMoonLightingEnabled())
        visibility.y=geAtmTerrainVisibility(worldPosition,geAtmShadow.y)
            *geAtmCloudVisibility(worldPosition,normalize(geAtmMoonDirection.xyz),true);
    return visibility;
}

vec3 geAtmMediumLight(vec3 worldPosition,vec3 planetPosition,vec3 direction,
    vec3 sigmaScattering,float phaseG,vec2 visibility)
{
    vec3 source=vec3(0.0);
    if(geAtmSunLightingEnabled())
    {
        vec3 light=normalize(geAtmSunDirection.xyz);
        vec3 direct=geAtmSunTransmittance(planetPosition,light)
            *geAtmMiePhase(dot(direction,light),phaseG)*visibility.x;
        vec3 ambient=geAtmMultipleScattering(planetPosition,light)*max(geAtmMedium.w,0.0);
        source+=sigmaScattering*geAtmSunRadiance.rgb*(direct+ambient);
    }
    if(geAtmMoonLightingEnabled())
    {
        vec3 light=normalize(geAtmMoonDirection.xyz);
        vec3 direct=geAtmSunTransmittance(planetPosition,light)
            *geAtmMiePhase(dot(direction,light),phaseG)*visibility.y;
        vec3 ambient=geAtmMultipleScattering(planetPosition,light)*max(geAtmMedium.w,0.0);
        source+=sigmaScattering*geAtmMoonRadiance.rgb*(direct+ambient);
    }
    return geAtmArtistic(source,direction);
}
#endif
