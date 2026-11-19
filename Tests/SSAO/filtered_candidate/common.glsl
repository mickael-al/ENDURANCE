#ifndef GE_SSAO_COMMON_GLSL
#define GE_SSAO_COMMON_GLSL
#include "../../../Asset/Shader/Geometry/gbuffer_position.glsl"
#include "../../../Asset/Shader/Geometry/gbuffer_geometry.glsl"

layout(std430,binding=0) readonly buffer UniformBufferCamera
{
    vec3 camPos;
    mat4 view;
    mat4 invView;
    mat4 proj;
} ubc;
layout(binding=0) uniform sampler2D geSsaoPosition;
layout(binding=1) uniform sampler2D geSsaoNormal;

const float GE_SSAO_RADIUS=0.5;
const float GE_SSAO_BIAS=0.025;
const int GE_SSAO_SAMPLES=64;

bool geSsaoGeometry(vec2 encoded)
{
    return encoded.x<0.0 && encoded.y!=-2.0
        && !any(isnan(encoded)) && !any(isinf(encoded));
}

bool geSsaoReceiver(vec2 encoded)
{
    // Emissive surfaces do not consume AO, but remain occluders for neighbours.
    return geSsaoGeometry(encoded) && encoded.y<=0.0;
}

vec3 geSsaoPositionAt(ivec2 pixel,vec2 encoded,ivec2 size)
{
    vec2 ndc=vec2(pixel*2+1-size)/vec2(size);
    return geGBufferViewPositionNDC(ndc,encoded.x,ubc.proj);
}

bool geSsaoViewNormal(ivec2 pixel,float tag,out vec3 normal)
{
    if(geGBufferGeometricNormal(tag,normal)) return true;
    // Material normals in this engine are WORLD-space. Positions are VIEW-space.
    normal=mat3(ubc.view)*texelFetch(geSsaoNormal,pixel,0).xyz;
    float length2=dot(normal,normal);
    if(!(length2>1e-12) || any(isnan(normal)) || any(isinf(normal))) return false;
    normal*=inversesqrt(length2);
    return true;
}

#endif
