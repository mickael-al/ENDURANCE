#version 450 core
#include "../../Asset/Shader/Atmosphere/sample.glsl"
layout(local_size_x=16) in;
layout(std430,binding=0) writeonly buffer Results { vec4 result[]; };

void main()
{
    uint i=gl_GlobalInvocationID.x;
    vec3 surface=vec3(4.0,2.0,1.0);
    if(i<9u)
    {
        float distance=geAtmVolume.x*pow(float(i)/8.0,2.0);
        vec3 world=geAtmCamera.xyz+vec3(0,0,distance);
        result[i]=vec4(geAtmComposite(surface,world,vec2(0.5)),1.0);
    }
    else if(i==9u)
        result[i]=vec4(geAtmComposite(surface,geAtmCamera.xyz+vec3(0,0,2.0*geAtmVolume.x),vec2(0.5)),1);
    else if(i==10u)
        result[i]=vec4(geAtmCompositeSky(surface,vec2(0.5)),1.0);
    else
        result[i]=vec4(0);
}
