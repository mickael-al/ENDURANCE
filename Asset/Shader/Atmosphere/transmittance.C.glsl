#version 450 core
#include "physical.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform image2D geAtmOutput;
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geAtmOutput);
    if(any(greaterThanEqual(pixel,size))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(size);
    float radius=geAtmRadii.x+max(uv.y*uv.y*(geAtmRadii.y-geAtmRadii.x),0.1);
    float horizon=geAtmHorizonCosine(radius);
    float mu=mix(horizon,1.0,uv.x*uv.x);
    vec3 origin=vec3(0.0,radius,0.0),direction=vec3(sqrt(max(1.0-mu*mu,0.0)),mu,0.0);
    float end=geAtmSphere(origin,direction,geAtmRadii.y).y;
    int count=clamp(geAtmQuality.y,8,256);
    float stepLength=max(end,0.0)/float(count);
    vec3 opticalDepth=vec3(0.0);
    for(int i=0;i<count;++i)
    {
        vec3 r,m,e;
        geAtmMediumAt(origin+direction*((float(i)+0.5)*stepLength),r,m,e);
        opticalDepth+=e*stepLength;
    }
    imageStore(geAtmOutput,pixel,vec4(exp(-opticalDepth),1.0));
}
