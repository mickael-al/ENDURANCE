#version 460 core

// Each vertex is 12 bytes: XYZ uint16 scaled by 65534, climate UNORM8,
// and an octahedral normal SNORM16x2. Code 65535 is unused so 2:1 LOD
// boundaries have the same quantized positions. Descriptors stay GPU resident.
layout(location = 0) in uint packedXY;
layout(location = 1) in uint packedZClimate;
layout(location = 2) in uint packedNormal;

layout(std430, binding = 0) readonly buffer UniformBufferCamera
{
    vec3 camPos;
    mat4 view;
    mat4 invView;
    mat4 proj;
} camera;

struct ChunkDescriptor
{
    vec4 originExtent;
    uvec4 metadata;
};
layout(std430, binding = 8) readonly buffer TerrainChunks
{
    ChunkDescriptor chunks[];
};

layout(location = 0) out vec3 viewPosition;
layout(location = 1) out vec3 worldPosition;
layout(location = 2) out vec3 worldNormal;
layout(location = 3) out vec2 climate;
layout(location = 4) flat out uint chunkLod;

vec3 decodeNormal(uint encodedNormal)
{
    vec2 oct = unpackSnorm2x16(encodedNormal);
    vec3 n = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float fold = max(-n.z, 0.0);
    n.xy += vec2(n.x >= 0.0 ? -fold : fold, n.y >= 0.0 ? -fold : fold);
    return normalize(n);
}

vec3 decodeWorldPosition(ChunkDescriptor chunk)
{
    vec3 q = vec3(packedXY & 65535u, packedXY >> 16u, packedZClimate & 65535u);
    float lodScale = exp2(float(chunk.metadata.x));
    // metadata.yzw stores signed origin/cells on the finest lattice. Computing
    // this shared integer numerator with one FMA gives both LODs exactly the
    // same rounding at a seam, without expanding vertices or requiring FP64.
    precise vec3 numerator = fma(vec3(ivec3(chunk.metadata.yzw)), vec3(65534.0), q * lodScale);
    precise float baseExtent = chunk.originExtent.w / lodScale;
    precise vec3 position = numerator * (baseExtent / 65534.0);
    return position;
}

void main()
{
    ChunkDescriptor chunk = chunks[gl_BaseInstance];
    worldPosition = decodeWorldPosition(chunk);
    worldNormal = decodeNormal(packedNormal);
    climate = vec2((packedZClimate >> 16u) & 255u,
                   packedZClimate >> 24u) * (1.0 / 255.0);
    chunkLod = chunk.metadata.x;
    vec4 position = camera.view * vec4(worldPosition, 1.0);
    viewPosition = position.xyz;
    gl_Position = camera.proj * position;
}
