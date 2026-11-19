#version 450 core
#include "physical.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform image2D geAtmOutput;
void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geAtmOutput);
    if(any(greaterThanEqual(pixel,size))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(size);
    float radius=geAtmRadii.x+max(uv.y*uv.y*(geAtmRadii.y-geAtmRadii.x),1.0);
    float mu=uv.x*2.0-1.0;
    vec3 origin=vec3(0.0,radius,0.0),sun=vec3(sqrt(max(1.0-mu*mu,0.0)),mu,0.0);
    vec3 total=vec3(0.0),feedback=vec3(0.0);
    int directions=clamp(geAtmQuality.z,4,128),steps=clamp(geAtmQuality.w,4,128);
    for(int d=0;d<directions;++d)
    {
        float y=1.0-2.0*(float(d)+0.5)/float(directions);
        float phi=float(d)*2.399963229728653;
        vec3 direction=vec3(sqrt(max(1.0-y*y,0.0))*cos(phi),y,sqrt(max(1.0-y*y,0.0))*sin(phi));
        float distance=geAtmPath(origin,direction).y;
        vec3 t=vec3(1.0),radiance=vec3(0.0),returnFactor=vec3(0.0);
        for(int i=0;i<steps;++i)
        {
            float a=float(i)/float(steps),b=float(i+1)/float(steps);
            float begin=distance*a*a,end=distance*b*b;
            vec3 p=origin+direction*((begin+end)*0.5),r,m,e;
            geAtmMediumAt(p,r,m,e);
            vec3 integral=geAtmIntegral(e,end-begin);
            float cosine=dot(direction,sun);
            radiance+=t*geAtmSunTransmittance(p,sun)
                *(r*geAtmRayleighPhase(cosine)+m*geAtmMiePhase(cosine,geAtmMedium.y))*integral;
            returnFactor+=t*(r+m)*integral;
            t*=exp(-e*(end-begin));
        }
        vec3 endpoint=origin+direction*distance;
        if(length(endpoint)<geAtmRadii.x+10.0)
        {
            vec3 normal=normalize(endpoint);
            radiance+=t*geAtmSunTransmittance(endpoint+normal,sun)*max(dot(normal,sun),0.0)
                *clamp(geAtmArt.w,0.0,0.99)/GE_ATM_PI;
            returnFactor+=t*clamp(geAtmArt.w,0.0,0.99);
        }
        total+=radiance;
        feedback+=returnFactor;
    }
    // Isotropic geometric-series closure, following Hillaire's low-dimensional
    // multiple-scattering approximation. It is not full spectral path tracing.
    total/=float(directions); feedback/=float(directions);
    vec3 multiple=total/max(vec3(1.0)-min(feedback,vec3(0.98)),vec3(0.02));
    imageStore(geAtmOutput,pixel,vec4(max(multiple,vec3(0.0)),1.0));
}
