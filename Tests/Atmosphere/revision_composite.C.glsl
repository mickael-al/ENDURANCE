#version 450 core
#include "../../Asset/Shader/Atmosphere/sample.glsl"
#include "../../Asset/Shader/Atmosphere/sky_evaluate.glsl"
layout(local_size_x=8,local_size_y=8) in;
layout(rgba32f,binding=0) writeonly uniform image2D geRevisionRawSky;
layout(rgba32f,binding=1) writeonly uniform image2D geRevisionComposedSky;
void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geRevisionRawSky);
    if(any(greaterThanEqual(p,size))) return;
    vec2 uv=(vec2(p)+0.5)/vec2(size);
    vec3 sky=geSkyRadiance(geAtmViewDirection(uv),false,true);
    imageStore(geRevisionRawSky,p,vec4(sky,1.0));
    imageStore(geRevisionComposedSky,p,vec4(geAtmCompositeSky(sky,uv),1.0));
}
