#version 450 core
#include "../../Asset/Shader/Atmosphere/sky_evaluate.glsl"
layout(local_size_x=8,local_size_y=8) in;
layout(rgba32f,binding=0) writeonly uniform image2D geRevisionCurrentSky;
void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geRevisionCurrentSky);
    if(any(greaterThanEqual(p,size))) return;
    vec2 uv=(vec2(p)+0.5)/vec2(size);
    imageStore(geRevisionCurrentSky,p,vec4(geSkyRadiance(geAtmViewDirection(uv),false,true),1.0));
}
