// Shared deferred/forward sampling. A transparent material supplies its own
// world position; it must never sample using the opaque background depth.
#ifndef GE_ATMOSPHERE_SAMPLE_GLSL
#define GE_ATMOSPHERE_SAMPLE_GLSL
#define GE_ATM_TRANSMITTANCE_BINDING 15
#define GE_ATM_MULTISCATTER_BINDING 11
#define GE_ATM_CLOUD_SHADOW_BINDING 14
// cloud_common's noise sampler is not used by the material helpers, so the
// optimiser removes it; unit3 remains available for material roughness.
#include "physical.glsl"
#include "cloud_common.glsl"
layout(binding=9) uniform sampler2D geAtmBaselineScattering;
layout(binding=10) uniform sampler2D geAtmBaselineTransmission;
layout(binding=12) uniform sampler3D geAtmScatteringVolume;
layout(binding=13) uniform sampler3D geAtmTransmissionVolume;
uniform bool uAtmosphereActive;

vec3 geAtmVolumeCoordinate(vec2 screenUV,float distance)
{
    ivec3 size=textureSize(geAtmScatteringVolume,0);
    float slice=sqrt(clamp(distance/max(geAtmVolume.x,1.0),0.0,1.0))*float(max(size.z-1,1));
    return vec3(clamp(screenUV,vec2(0.0),vec2(1.0)),(slice+0.5)/float(max(size.z,1)));
}

void geAtmSample(vec2 screenUV,float distance,out vec3 scattering,out vec3 transmittance)
{
    scattering=vec3(0.0); transmittance=vec3(1.0);
    if(!uAtmosphereActive || distance<=0.0) return;
    vec3 uvw=geAtmVolumeCoordinate(screenUV,distance);
    scattering=max(textureLod(geAtmScatteringVolume,uvw,0.0).rgb,vec3(0.0));
    transmittance=clamp(textureLod(geAtmTransmissionVolume,uvw,0.0).rgb,vec3(0.0),vec3(1.0));
}

vec3 geAtmComposite(vec3 surfaceRadiance,vec3 worldPosition,vec2 screenUV)
{
    if(!uAtmosphereActive) return surfaceRadiance;
    vec3 s,t;
    geAtmSample(screenUV,length(worldPosition-geAtmCamera.xyz),s,t);
    // Visible-atmosphere saturation changes the scattered light, not material
    // reflectance or Beer-Lambert transmission. IBL has its own control.
    return max(surfaceRadiance,vec3(0.0))*t+geAtmSaturate(s,geAtmArt.y);
}

vec3 geAtmCompositeSky(vec3 skyRadiance,vec2 screenUV)
{
    if(!uAtmosphereActive) return geAtmSaturate(skyRadiance,geAtmArt.y);
    if(geAtmControl.y==0 && geAtmControl.w==0) return geAtmSaturate(skyRadiance,geAtmArt.y);
    vec3 s,t; geAtmSample(screenUV,geAtmVolume.x,s,t);
    vec3 baseS=max(textureLod(geAtmBaselineScattering,screenUV,0.0).rgb,vec3(0.0));
    vec3 baseT=clamp(textureLod(geAtmBaselineTransmission,screenUV,0.0).rgb,vec3(0.0),vec3(1.0));
    // Multiplying the residual by T/Tbase is stable even inside a thick cloud;
    // reconstructing an arbitrarily bright tail via division alone is not.
    vec3 ratio=clamp(t/max(baseT,vec3(0.001)),vec3(0.0),vec3(1.0));
    // Keep the subtraction signed until final composition. Clamping the tail
    // first would brighten clear rays when the two LUT quadratures disagree.
    return geAtmSaturate(max(s+(skyRadiance-baseS)*ratio,vec3(0.0)),geAtmArt.y);
}

vec3 geAtmDirectTransmission(vec3 worldPosition,vec3 lightDirection)
{
    if(!uAtmosphereActive) return vec3(1.0);
    vec3 position=geAtmOrigin()+(worldPosition-geAtmCamera.xyz);
    bool secondary=geAtmMoonDirection.w>0.5 &&
        dot(lightDirection,geAtmMoonDirection.xyz)>dot(lightDirection,geAtmSunDirection.xyz);
    return geAtmSunTransmittance(position,normalize(lightDirection))
        *geAtmCloudVisibility(worldPosition,normalize(lightDirection),secondary);
}
#endif
