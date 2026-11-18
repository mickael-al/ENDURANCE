#version 450 core
#include "cubemap_common.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(binding=0) uniform samplerCube geBlendFrom;
layout(binding=1) uniform samplerCube geBlendTo;
layout(rgba16f,binding=0) writeonly uniform imageCube geBlendOutput;
uniform float geBlendFactor;
uniform float geBlendLevel;
void main()
{
    ivec3 p=ivec3(gl_GlobalInvocationID);
    ivec2 size=imageSize(geBlendOutput);
    if(any(greaterThanEqual(p.xy,size))||p.z>=6) return;
    vec3 d=geCubeDirection(p,size);
    vec3 value=mix(textureLod(geBlendFrom,d,geBlendLevel).rgb,
        textureLod(geBlendTo,d,geBlendLevel).rgb,clamp(geBlendFactor,0.0,1.0));
    imageStore(geBlendOutput,p,vec4(value,1));
}
