#version 450 core
#include "sky_evaluate.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform image2D geAtmVisibleCloudOutput;

void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geAtmVisibleCloudOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec3 delta=vec3(0.0);
    if(geAtmHasClouds())
    {
        vec3 direction=geAtmViewDirection((vec2(p)+0.5)/vec2(size));
        vec3 clear=max(textureLod(geAtmSkyView,geAtmSkyUV(direction),0.0).rgb,vec3(0.0));
        // Only the cloud contribution is reconstructed from a reduced view.
        // Clear-air gradients and sub-pixel solar/lunar disks retain the
        // full display resolution. Camera, wind and sources are CURRENT;
        // this pass never samples the delayed lighting cubemap.
        delta=geSkyRadiance(direction,false,true)-clear;
    }
    imageStore(geAtmVisibleCloudOutput,p,vec4(clamp(delta,vec3(-65000.0),vec3(65000.0)),1.0));
}
