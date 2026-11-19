// Generic directional surface shadows. Bias is measured in shadow texels,
// independent of atmosphere state and of the cascade's caster depth margin.
// A local receiver plane must NOT be extrapolated across neighbouring facets:
// the deferred G-buffer derivatives can themselves cross a triangle boundary.
#ifndef GE_DIRECTIONAL_SURFACE_SHADOW_GLSL
#define GE_DIRECTIONAL_SURFACE_SHADOW_GLSL
#include "shadow_filter.glsl"

vec3 geDirectionalSurfaceNormal(vec3 worldDx,vec3 worldDy)
{
    float scale=max(max(length(worldDx),length(worldDy)),1e-20);
    vec3 normal=cross(worldDx/scale,worldDy/scale);
    float norm2=dot(normal,normal);
    return norm2>1e-10?normal*inversesqrt(norm2):vec3(0.0);
}

vec3 geDirectionalSurfaceOffsetFromNormal(mat4 projectionView,ivec2 resolution,
    vec3 normal,vec2 biasTexels)
{
    vec3 rowX=vec3(projectionView[0][0],projectionView[1][0],projectionView[2][0]);
    vec3 rowY=vec3(projectionView[0][1],projectionView[1][1],projectionView[2][1]);
    vec3 rowZ=vec3(projectionView[0][2],projectionView[1][2],projectionView[2][2]);
    vec3 toLight=-normalize(rowZ);
    // The deferred path supplies the normal captured on the actual primitive.
    // Forward surfaces use their primitive derivatives. Neither includes a
    // material normal map. Degenerate geometry falls back towards the light.
    float norm2=dot(normal,normal);
    normal=norm2>1e-10?normal*inversesqrt(norm2):toLight;
    if(dot(normal,toLight)<0.0) normal=-normal;
    float cosine=clamp(dot(normal,toLight),0.0,1.0);
    float sine=sqrt(max(0.0,1.0-cosine*cosine));
    float worldTexel=max(2.0/(length(rowX)*float(resolution.x)),
                         2.0/(length(rowY)*float(resolution.y)));
    // A 2x2 footprint can straddle facets with different slopes. Keep a
    // conservative base (the half-diagonal of a texel) even on head-on
    // facets; a pure sine factor would vanish and reintroduce crease acne.
    const float footprintFloor=0.70710678;
    vec2 amount=clamp(biasTexels,vec2(0.0),vec2(8.0));
    return worldTexel*(toLight*amount.x+normal*amount.y*max(sine,footprintFloor));
}

vec3 geDirectionalSurfaceOffset(mat4 projectionView,ivec2 resolution,
    vec3 worldDx,vec3 worldDy,vec2 biasTexels)
{
    return geDirectionalSurfaceOffsetFromNormal(projectionView,resolution,
        geDirectionalSurfaceNormal(worldDx,worldDy),biasTexels);
}

float geDirectionalSurfaceNumericalBias(mat4 projectionView,float depth,vec3 worldPositionError)
{
    vec3 rowZ=vec3(projectionView[0][2],projectionView[1][2],projectionView[2][2]);
    float depthPerMetre=0.5*length(rowZ);
    float positionMargin=0.5*dot(max(worldPositionError,vec3(0.0)),abs(rowZ));
    float rounding=2.0*1.1920928955078125e-7*max(abs(depth),0.25);
    return max(0.005*depthPerMetre+positionMargin,rounding);
}

float geDirectionalSurfaceShadowFromNormal(sampler2DArray depthMap,vec4 shadowCoord,int layer,
    mat4 projectionView,vec3 worldNormal,vec3 worldPositionError,vec2 biasTexels)
{
    if(!(shadowCoord.w>0.0) || any(isnan(shadowCoord)) || any(isinf(shadowCoord))) return 1.0;
    vec3 receiver=shadowCoord.xyz/shadowCoord.w;
    ivec3 size=textureSize(depthMap,0);
    if(any(lessThanEqual(size,ivec3(0))) || layer<0 || layer>=size.z ||
        any(lessThan(receiver,vec3(0.0))) || any(greaterThan(receiver,vec3(1.0)))) return 1.0;
    vec3 offset=geDirectionalSurfaceOffsetFromNormal(projectionView,size.xy,worldNormal,biasTexels);
    receiver+=(mat3(projectionView)*offset)*(0.5/shadowCoord.w);
    if(any(isnan(receiver)) || any(isinf(receiver)) || receiver.z<0.0 || receiver.z>1.0) return 1.0;
    float bias=geDirectionalSurfaceNumericalBias(projectionView,receiver.z,worldPositionError);
    return geShadowPCF(depthMap,receiver,layer,bias);
}

float geDirectionalSurfaceShadow(sampler2DArray depthMap,vec4 shadowCoord,int layer,
    mat4 projectionView,vec3 worldDx,vec3 worldDy,vec3 worldPositionError,vec2 biasTexels)
{
    return geDirectionalSurfaceShadowFromNormal(depthMap,shadowCoord,layer,projectionView,
        geDirectionalSurfaceNormal(worldDx,worldDy),worldPositionError,biasTexels);
}

// Default for standalone engine consumers; scene lights supply their own
// values through the overload above (UniformBufferLight / LightData).
float geDirectionalSurfaceShadow(sampler2DArray depthMap,vec4 shadowCoord,int layer,
    mat4 projectionView,vec3 worldDx,vec3 worldDy,vec3 worldPositionError)
{
    return geDirectionalSurfaceShadow(depthMap,shadowCoord,layer,projectionView,
        worldDx,worldDy,worldPositionError,vec2(0.5,1.5));
}
#endif
