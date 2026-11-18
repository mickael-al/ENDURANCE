#version 430 core
#include "../../Asset/Shader/Shadow/directional_surface.glsl"
layout(binding=0) uniform sampler2DArray depthMap;
uniform mat4 projectionView;
uniform mat4 inverseProjectionView;
uniform vec3 plane;
uniform vec4 receiverDomain;
uniform int layer;
uniform int degenerateMode;
uniform float homogeneousW;
uniform vec3 worldPositionError;
uniform vec2 biasTexels;
in vec2 TexCoords;
layout(location=0) out vec4 visibility;
layout(location=1) out vec4 diagnostics;
layout(location=2) out vec4 geometry;

void main()
{
    vec2 uv=receiverDomain.xy+TexCoords*receiverDomain.zw;
    // Construct the actual plane before normalized-depth quantization and
    // differentiate in a relative frame, like the deferred view-space path.
    vec3 relative=mat3(inverseProjectionView)*vec3(2.0*uv-1.0,2.0*dot(plane.xy,uv-0.5));
    vec3 world=relative+inverseProjectionView[3].xyz+
        inverseProjectionView[2].xyz*(2.0*plane.z-1.0);
    vec3 dx=dFdx(relative),dy=dFdy(relative);
    if(degenerateMode==1) { dx=vec3(0);dy=vec3(0); }
    if(degenerateMode==2) dy=dx;
    vec4 projected=projectionView*vec4(world,1.0);
    vec3 receiver=projected.xyz*0.5+0.5;
    vec4 sc=vec4(receiver,1.0)*homogeneousW;
    float result=geDirectionalSurfaceShadow(depthMap,sc,layer,projectionView,
        dx,dy,worldPositionError,biasTexels);

    // Expose the real production offset and sample location. The CPU oracle
    // independently integrates a binary rectangular occluder's tent footprint;
    // it does not duplicate depth comparisons or this helper's tap loop.
    vec3 offset=geDirectionalSurfaceOffset(projectionView,textureSize(depthMap,0).xy,
        dx,dy,biasTexels);
    vec3 sampled=receiver;
    if(homogeneousW>0.0) sampled+=(mat3(projectionView)*offset)*(0.5/homogeneousW);
    float numerical=geDirectionalSurfaceNumericalBias(projectionView,sampled.z,worldPositionError);
    visibility=vec4(result,numerical,length(offset),receiver.z);
    diagnostics=vec4(sampled,0.0);
    geometry=vec4(offset,0.0);
}
