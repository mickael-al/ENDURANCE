// Signed implicit primitives for localized terrain additions AND excavations.
#ifndef ENDURANCE_BIOME_LANDFORMS_3D_GLSL
#define ENDURANCE_BIOME_LANDFORMS_3D_GLSL
#include "biome_landforms_common.glsl"
#include "../Noise/noise_3d.glsl"

bool landVolumeEnabled(FieldColumn column)
{
    return uFieldMode==0u && uDetailScale>0.0 && column.terrain.w>0.0002;
}

vec3 landVolumeLocal(vec3 p,FieldColumn column,LandSite site)
{
    float y=(p.y-column.terrain.x-column.blend.y)/max(uDetailScale,0.000001)
        -clamp(column.blend.z,-160.0,160.0);
    return vec3(site.q.x,y,site.q.y);
}

// Positive outside. Ellipsoid is a conservative distance-like field, not an
// exact distance; marching cubes needs its continuous zero set, not ray steps.
float landEllipsoid(vec3 p,vec3 radii)
{
    vec3 r=max(radii,vec3(0.001));
    return (length(p/r)-1.0)*min(r.x,min(r.y,r.z));
}

float landBox(vec3 p,vec3 halfSize)
{
    vec3 q=abs(p)-max(halfSize,vec3(0.001));
    return length(max(q,0.0))+min(max(q.x,max(q.y,q.z)),0.0);
}

float landCapsule(vec3 p,vec3 a,vec3 b,float radius)
{
    vec3 pa=p-a,ba=b-a;
    float t=clamp(dot(pa,ba)/max(dot(ba,ba),0.000001),0.0,1.0);
    return length(pa-ba*t)-max(radius,0.001);
}

// Smooth MAX for positive-solid densities (opposite sign to usual SDF min).
float landUnion(float a,float b,float radius)
{
    float k=max(radius,0.001);
    float h=clamp(0.5+0.5*(a-b)/k,0.0,1.0);
    return mix(b,a,h)+k*h*(1.0-h);
}

float landDelta(vec3 p,FieldColumn column,float target,float siteMask)
{
    if(!landVolumeEnabled(column) || siteMask<=0.0) return 0.0;
    float halfWidth=0.5*column.terrain.w;
    float center=column.terrain.x+column.blend.y
        +clamp(column.blend.z,-160.0,160.0)*uDetailScale;
    float edge=abs(p.y-center)/halfWidth;
    // Vanish within the contract before prepare/accumulate culls the band.
    float influence=(1.0-smoothstep(0.76,0.98,edge))*clamp(siteMask,0.0,1.0);
    return (target-(column.terrain.x-p.y))*influence;
}
#endif
