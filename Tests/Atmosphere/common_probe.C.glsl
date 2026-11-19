#version 430 core
#include "../../Asset/Shader/Atmosphere/atmosphere_common.glsl"
layout(local_size_x=32) in;
layout(std430,binding=0) writeonly buffer Results { vec4 result[]; };

void main()
{
    uint i=gl_GlobalInvocationID.x;
    float distance=float(i)*13.5;
    vec3 source=vec3(0.07,0.02,0.11), extinction=vec3(0.02,0.04,0.08);
    vec3 s=vec3(0),t=vec3(1);
    geAtmIntegrate(s,t,source,extinction,distance);
    result[i*9u+0u]=vec4(s,1);
    result[i*9u+1u]=vec4(t,1);
    result[i*9u+2u]=vec4(geAtmIntegral(vec3(0),distance),1);
    result[i*9u+3u]=vec4(geAtmArtistic(vec3(0),vec3(0,1,0)),1);
    float z=1.0-2.0*(float(i)+0.5)/32.0;
    float angle=float(i)*2.399963229728653;
    vec3 direction=vec3(sqrt(1.0-z*z)*cos(angle),z,sqrt(1.0-z*z)*sin(angle));
    vec3 reconstructed=geAtmSkyDirection(geAtmSkyUV(direction));
    result[i*9u+4u]=vec4(reconstructed,dot(direction,reconstructed));
    vec3 rayleigh,mie,medium;
    geAtmMediumAt(vec3(0,geAtmRadii.x+float(i)*3000.0,0),rayleigh,mie,medium);
    result[i*9u+5u]=vec4(rayleigh,geAtmRayleighPhase(z));
    result[i*9u+6u]=vec4(mie,geAtmMiePhase(z,geAtmMedium.y));
    result[i*9u+7u]=vec4(medium,1);
    float radius=exp(mix(log(1e-5),log(0.00465),float(i)/31.0));
    vec3 sourceDirection=vec3(0,1,0);
    result[i*9u+8u]=vec4(
        geAtmDiskMask(sourceDirection,sourceDirection,radius),
        geAtmDiskMask(vec3(sin(radius),cos(radius),0),sourceDirection,radius),
        geAtmDiskMask(vec3(sin(2.0*radius),cos(2.0*radius),0),sourceDirection,radius),
        geAtmDiskMask(sourceDirection,sourceDirection,0));
}
