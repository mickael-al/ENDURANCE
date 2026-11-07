#version 460 core

layout(location = 0) in uint packedXY;
layout(location = 1) in uint packedZClimate;

struct StructShadow
{
    mat4 projview;
    vec3 pos;
    float splitDepth;
};
layout(std430, binding = 0) readonly buffer UniformBufferShadow
{
    StructShadow shadows[];
};
struct ChunkDescriptor
{
    vec4 originExtent;
    uvec4 metadata;
};
layout(std430, binding = 8) readonly buffer TerrainChunks
{
    ChunkDescriptor chunks[];
};
uniform int offsetShadow;

vec3 decodeWorldPosition(ChunkDescriptor chunk)
{
    vec3 q = vec3(packedXY & 65535u, packedXY >> 16u, packedZClimate & 65535u);
    float lodScale = exp2(float(chunk.metadata.x));
    // Identical canonical numerator and rounding to the G-buffer vertex pass.
    precise vec3 numerator = fma(vec3(ivec3(chunk.metadata.yzw)), vec3(65534.0), q * lodScale);
    precise float baseExtent = chunk.originExtent.w / lodScale;
    precise vec3 position = numerator * (baseExtent / 65534.0);
    return position;
}

void main()
{
    vec3 position = decodeWorldPosition(chunks[gl_BaseInstance]);
    gl_Position = shadows[offsetShadow].projview * vec4(position, 1.0);
}
