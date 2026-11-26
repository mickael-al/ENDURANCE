#ifndef GE_MOON_SURFACE_GLSL
#define GE_MOON_SURFACE_GLSL
#include "atmosphere_common.glsl"
#ifndef GE_ATM_MOON_SURFACE
#define GE_ATM_MOON_SURFACE 0
#endif
#ifndef GE_ATM_MOON_MESH
#define GE_ATM_MOON_MESH 0
#endif
#if GE_ATM_MOON_SURFACE
layout(std140,binding=7) uniform GeMoonParameters
{
    vec4 geMoonCenterRadius;
    vec4 geMoonSurface; // rotation cos/sin, normal strength, atlas present
#if GE_ATM_MOON_MESH
    vec4 geMoonMeshRight;   // XYZ world basis, W tan(half FOV)
    vec4 geMoonMeshUp;
    vec4 geMoonMeshForward; // W cache ready
#endif
};
layout(binding=4) uniform sampler2D geMoonAtlas;
uniform float geMoonFootprint; // angular pixel footprint, also set for cube tiles

#if GE_ATM_MOON_MESH
// Actual geometry is rasterized only when its view/light changes. The current
// screen and the frozen IBL capture sample their own immutable image/projection.
vec4 geMoonMeshSample(vec3 direction)
{
    float forward=dot(direction,geMoonMeshForward.xyz);
    if(geMoonMeshForward.w<0.5 || forward<=0.0) return vec4(0.0);
    vec2 projected=vec2(dot(direction,geMoonMeshRight.xyz),dot(direction,geMoonMeshUp.xyz));
    vec2 halfFov=max(vec2(geMoonMeshRight.w,geMoonMeshUp.w),vec2(1e-6));
    vec2 uv=0.5+0.5*projected/(forward*halfFov);
    if(any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return vec4(0.0);
    float footprint=geMoonFootprint*float(textureSize(geMoonAtlas,0).x)
        /max(2.0*halfFov.x*forward*forward,1e-6);
    float lod=clamp(log2(max(footprint,1.0)),0.0,float(textureQueryLevels(geMoonAtlas)-1));
    return textureLod(geMoonAtlas,uv,lod);
}
float geMoonCoverage(vec3 direction)
{
    return clamp(geMoonMeshSample(direction).a,0.0,1.0);
}
vec3 geMoonRadiance(vec3 direction)
{
    if(geAtmSunDirection.w<0.5) return vec3(0.0);
    // Premultiplied albedo * Lambert / pi and self-shadowing are cached.
    // Solar intensity/tint stay live; never gate this by the observer's daylight.
    return geMoonMeshSample(direction).rgb*geAtmSunRadiance.rgb*geAtmMoonDisk.rgb;
}
#else
float geMoonCoverage(vec3 direction)
{
    float distance=length(geMoonCenterRadius.xyz);
    if(geMoonCenterRadius.w<=0.0 || distance<=geMoonCenterRadius.w) return 0.0;
    vec3 centre=geMoonCenterRadius.xyz/distance;
    if(dot(centre,direction)<=0.0) return 0.0;
    float radius=geMoonCenterRadius.w/distance;
    float edge=max(geMoonFootprint*0.65,1e-6);
    return 1.0-smoothstep(radius-edge,radius+edge,length(cross(centre,direction)));
}

vec3 geMoonToLocal(vec3 p)
{
    return vec3(geMoonSurface.x*p.x-geMoonSurface.y*p.z,p.y,
                geMoonSurface.y*p.x+geMoonSurface.x*p.z);
}
vec3 geMoonToWorld(vec3 p)
{
    return vec3(geMoonSurface.x*p.x+geMoonSurface.y*p.z,p.y,
                -geMoonSurface.y*p.x+geMoonSurface.x*p.z);
}

vec3 geMoonRadiance(vec3 direction)
{
    float coverage=geMoonCoverage(direction);
    if(coverage<=0.0 || geAtmSunDirection.w<0.5) return vec3(0.0);
    // Normalize distances BEFORE intersection: precision stays independent of
    // the orbit's metre scale. There is no iterative raymarch or runtime noise.
    float distance=length(geMoonCenterRadius.xyz);
    vec3 centre=geMoonCenterRadius.xyz/distance;
    float radius=geMoonCenterRadius.w/distance;
    vec3 perpendicular=cross(centre,direction);
    float discriminant=max(radius*radius-dot(perpendicular,perpendicular),0.0);
    float t=dot(centre,direction)-sqrt(discriminant);
    vec3 geometric=normalize(direction*t-centre);
    vec3 local=geMoonToLocal(geometric);
    vec3 normal=geometric;
    float albedo=0.20;
    if(geMoonSurface.w>0.5)
    {
        vec2 uv=vec2(atan(local.z,local.x)/(2.0*GE_ATM_PI)+0.5,
                     asin(clamp(local.y,-1.0,1.0))/GE_ATM_PI+0.5);
        float grazing=max(sqrt(discriminant)/max(radius,1e-6),0.12);
        float footprint=geMoonFootprint*float(textureSize(geMoonAtlas,0).x)
            /max(2.0*GE_ATM_PI*radius*grazing,1e-6);
        float lod=clamp(log2(max(footprint,1.0)),0.0,float(textureQueryLevels(geMoonAtlas)-1));
        vec4 surface=textureLod(geMoonAtlas,uv,lod);
        vec3 mapped=geMoonToWorld(surface.rgb*2.0-1.0);
        float radial=dot(mapped,geometric);
        normal=normalize(geometric*max(radial,0.10)+geMoonSurface.z*(mapped-geometric*radial));
        albedo=surface.a;
    }
    vec3 light=normalize(geAtmSunDirection.xyz);
    // Relief cannot illuminate the far/night hemisphere across the terminator.
    float day=smoothstep(-0.015,0.04,dot(geometric,light));
    float diffuse=max(dot(normal,light),0.0)*day;
    return geAtmSunRadiance.rgb*geAtmMoonDisk.rgb*(albedo*diffuse*coverage/GE_ATM_PI);
}
#endif // GE_ATM_MOON_MESH
#else
float geMoonCoverage(vec3 direction) { return 0.0; }
vec3 geMoonRadiance(vec3 direction) { return vec3(0.0); }
#endif
#endif
