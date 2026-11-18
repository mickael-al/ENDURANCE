#version 450 core
#include "cloud_common.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rg16f,binding=0) writeonly uniform image2D geAtmShadowOutput;
void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geAtmShadowOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec2 uv=(vec2(p)+0.5)/vec2(size);
    vec2 xz=geAtmShadowOrigin.xy+(uv*2.0-1.0)*max(geAtmCloudWind.z,1.0);
    vec3 position=vec3(xz.x,geAtmCloudLayer.x+0.1,xz.y);
    vec2 transmission=vec2(1.0);
    if(geAtmControl.z!=0)
    {
        if(geAtmSunDirection.w>0.5)
            transmission.x=geAtmSunDirection.y>0.001
                ? geAtmCloudLightRay(position,normalize(geAtmSunDirection.xyz),geAtmSamples.z) : 0.0;
        if(geAtmMoonDirection.w>0.5)
            transmission.y=geAtmMoonDirection.y>0.001
                ? geAtmCloudLightRay(position,normalize(geAtmMoonDirection.xyz),geAtmSamples.z) : 0.0;
    }
    imageStore(geAtmShadowOutput,p,vec4(transmission,0.0,1.0));
}
