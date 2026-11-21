#version 450 core
#include "cloud_common.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rg16f,binding=0) writeonly uniform image2D geAtmShadowOutput;
void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geAtmShadowOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec2 uv=(vec2(p)+0.5)/vec2(size);
    vec2 xz=geAtmShadowOrigin.xy+(uv*2.0-1.0)*max(geAtmCloudOffset.z,1.0);
    vec2 transmission=vec2(1.0);
    if(geAtmHasClouds())
    {
        if(geAtmSunDirection.w>0.5)
        {
            vec3 light=normalize(geAtmSunDirection.xyz);
            transmission.x=geAtmCloudLightRay(vec3(xz.x,geAtmCloudShadowHeight(light),xz.y),light,geAtmSamples.z);
        }
        if(geAtmMoonDirection.w>0.5 && any(greaterThan(geAtmMoonRadiance.rgb,vec3(0))))
        {
            vec3 light=normalize(geAtmMoonDirection.xyz);
            transmission.y=geAtmCloudLightRay(vec3(xz.x,geAtmCloudShadowHeight(light),xz.y),light,geAtmSamples.z);
        }
    }
    imageStore(geAtmShadowOutput,p,vec4(transmission,0.0,1.0));
}
