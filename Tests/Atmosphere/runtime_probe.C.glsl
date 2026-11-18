#version 450 core
#include "../../Asset/Shader/Atmosphere/lighting.glsl"
layout(local_size_x=1) in;
layout(std430,binding=0) writeonly buffer Probe { vec4 values[]; };
void main()
{
    values[0]=vec4(geAtmComposite(vec3(1),geAtmCamera.xyz+vec3(0,0,1000),vec2(.5)),uAtmosphereActive?1:0);
    values[1]=vec4(geAtmosphereIbl(vec3(1,2,3)),uAtmosphereIblActive?1:0);
    values[2]=vec4(geAtmosphereDirect(vec3(1),uAtmosphereSunIndex,geAtmCamera.xyz,vec3(0,1,0)),1);
    values[3]=vec4(geAtmosphereDirect(vec3(1),uAtmosphereMoonIndex,geAtmCamera.xyz,vec3(0,1,0)),1);
    values[4]=vec4(uAtmosphereInvResolution,0,1);
}
