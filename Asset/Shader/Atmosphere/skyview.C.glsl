#version 450 core
#include "physical.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform image2D geAtmOutput;
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geAtmOutput);
    if(any(greaterThanEqual(pixel,size))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(size);
    imageStore(geAtmOutput,pixel,vec4(geAtmPhysicalSky(geAtmSkyDirection(uv)),1.0));
}
