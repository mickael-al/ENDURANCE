#version 450 core
#include "cubemap_common.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(binding=0) uniform samplerCube geFilterSource;
layout(rgba16f,binding=0) writeonly uniform imageCube geFilterOutput;
uniform int geFilterMode; // 0 cosine convolution E/pi; 1 importance sampled GGX
uniform int geFilterSamples;
uniform float geFilterRoughness;
uniform ivec3 geDispatchOffset;
const float GE_FILTER_PI=3.141592653589793;
void main()
{
    ivec3 p=ivec3(gl_GlobalInvocationID)+geDispatchOffset;
    ivec2 size=imageSize(geFilterOutput);
    if(any(greaterThanEqual(p.xy,size))||p.z>=6) return;
    vec3 n=geCubeDirection(p,size);
    if(geFilterMode==1 && geFilterRoughness<0.0001)
    {
        imageStore(geFilterOutput,p,vec4(textureLod(geFilterSource,n,0).rgb,1));
        return;
    }
    uint count=uint(clamp(geFilterSamples,8,1024));
    vec3 sum=vec3(0); float weight=0;
    float resolution=float(textureSize(geFilterSource,0).x);
    float texelSolidAngle=4.0*GE_FILTER_PI/(6.0*resolution*resolution);
    for(uint i=0u;i<count;++i)
    {
        vec2 xi=geHammersley(i,count);
        if(geFilterMode==0)
        {
            float phi=2.0*GE_FILTER_PI*xi.x;
            float r=sqrt(xi.y);
            vec3 l=geTangentBasis(n)*vec3(r*cos(phi),r*sin(phi),sqrt(1.0-xi.y));
            // Cosine-weighted hemisphere integration stores irradiance / PI.
            sum+=textureLod(geFilterSource,l,max(0.0,0.5*log2(6.0*resolution*resolution/float(count)))).rgb;
            weight+=1.0;
        }
        else
        {
            vec3 h=geImportanceGGX(xi,n,geFilterRoughness);
            vec3 l=normalize(2.0*dot(n,h)*h-n);
            float nl=max(dot(n,l),0.0);
            if(nl>0.0)
            {
                float nh=max(dot(n,h),0.0);
                float a=geFilterRoughness*geFilterRoughness;
                float denom=nh*nh*(a*a-1.0)+1.0;
                float distribution=a*a/max(GE_FILTER_PI*denom*denom,1e-8);
                float pdf=max(distribution*nh/(4.0*max(dot(h,n),0.0001)),0.0001);
                float lod=max(0.0,0.5*log2(1.0/(float(count)*pdf*texelSolidAngle)));
                sum+=textureLod(geFilterSource,l,lod).rgb*nl;
                weight+=nl;
            }
        }
    }
    imageStore(geFilterOutput,p,vec4(sum/max(weight,0.0001),1));
}
