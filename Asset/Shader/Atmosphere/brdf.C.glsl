#version 450 core
#include "cubemap_common.glsl"
layout(local_size_x=8,local_size_y=8) in;
layout(rg16f,binding=0) writeonly uniform image2D geBrdfOutput;
float geGeometryIBL(float nv,float roughness)
{
    float k=roughness*roughness*0.5;
    return nv/max(nv*(1.0-k)+k,0.00001);
}
void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geBrdfOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec2 uv=(vec2(p)+0.5)/vec2(size);
    float nv=uv.x,roughness=uv.y;
    vec3 v=vec3(sqrt(max(1.0-nv*nv,0.0)),0,nv),n=vec3(0,0,1);
    vec2 sum=vec2(0);
    const uint count=256u;
    for(uint i=0u;i<count;++i)
    {
        vec3 h=geImportanceGGX(geHammersley(i,count),n,roughness);
        vec3 l=normalize(2.0*dot(v,h)*h-v);
        float nl=max(l.z,0.0),nh=max(h.z,0.0),vh=max(dot(v,h),0.0);
        if(nl>0.0)
        {
            float visibility=geGeometryIBL(nv,roughness)*geGeometryIBL(nl,roughness)*vh/max(nh*nv,0.00001);
            float fresnel=pow(1.0-vh,5.0);
            sum+=vec2(1.0-fresnel,fresnel)*visibility;
        }
    }
    imageStore(geBrdfOutput,p,vec4(sum/float(count),0,1));
}
