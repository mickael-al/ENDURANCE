#version 450 core
#define GE_ATM_MOON_SURFACE 1
#include "../../Asset/Shader/Atmosphere/sky_evaluate.glsl"
in vec2 TexCoords;
layout(location=0) out vec4 color;
uniform vec3 probeForward;
void main()
{
    vec3 right=normalize(cross(probeForward,vec3(0,1,0)));
    vec3 up=normalize(cross(right,probeForward));
    vec2 p=(TexCoords*2.0-1.0)*0.31;
    vec3 direction=normalize(probeForward+right*p.x+up*p.y);
#ifdef PROBE_ATMOSPHERE
    vec3 radiance=geSkyMoon(direction);
#ifdef PROBE_BACKGROUND
    radiance+=geSkyRadiance(direction,false,true);
#endif
#else
    vec3 radiance=geMoonRadiance(direction);
#endif
    color=vec4(radiance,geMoonCoverage(direction));
}
