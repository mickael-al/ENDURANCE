#ifndef GE_ATMOSPHERE_CUBEMAP_GLSL
#define GE_ATMOSPHERE_CUBEMAP_GLSL
vec3 geCubeDirection(ivec3 p,ivec2 size)
{
    vec2 uv=(vec2(p.xy)+0.5)/vec2(size)*2.0-1.0;
    if(p.z==0) return normalize(vec3(1.0,-uv.y,-uv.x));
    if(p.z==1) return normalize(vec3(-1.0,-uv.y,uv.x));
    if(p.z==2) return normalize(vec3(uv.x,1.0,uv.y));
    if(p.z==3) return normalize(vec3(uv.x,-1.0,-uv.y));
    if(p.z==4) return normalize(vec3(uv.x,-uv.y,1.0));
    return normalize(vec3(-uv.x,-uv.y,-1.0));
}
vec2 geHammersley(uint i,uint count)
{
    return vec2(float(i)/float(count),float(bitfieldReverse(i))*2.3283064365386963e-10);
}
mat3 geTangentBasis(vec3 n)
{
    vec3 up=abs(n.z)<0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 t=normalize(cross(up,n));
    return mat3(t,cross(n,t),n);
}
vec3 geImportanceGGX(vec2 xi,vec3 n,float roughness)
{
    float a=max(roughness*roughness,0.0001);
    float phi=6.28318530718*xi.x;
    float cosTheta=sqrt((1.0-xi.y)/(1.0+(a*a-1.0)*xi.y));
    float sinTheta=sqrt(max(0.0,1.0-cosTheta*cosTheta));
    return normalize(geTangentBasis(n)*vec3(cos(phi)*sinTheta,sin(phi)*sinTheta,cosTheta));
}
#endif
