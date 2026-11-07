#version 450

layout(std430, binding = 0) buffer UniformBufferCamera
{
    vec3 camPos;
    mat4 view;
    mat4 invView;
    mat4 proj;
} ubc;

struct StructUBO { mat4 ubo; int mat_index; };
layout(std430, binding = 1) buffer UniformBufferObject
{
    StructUBO ubo[];
} objectData;

struct StructUBM
{
    vec4 albedo;
    vec2 offset;
    vec2 tilling;
    float metallic;
    float roughness;
    float normal;
    float ao;
    float emit;
};
layout(std430, binding = 2) buffer UniformBufferMaterials
{
    StructUBM ubm[];
} materialData;

// Binding 4/5 remain available for the material's shared skeletal palette.
// Cloth geometry is private to this actor and this ShapeBufferBase.
struct ClothVertex { vec4 position; vec4 normal; vec4 tangent; };
layout(std430, binding = 6) readonly buffer DeformedCloth
{
    ClothVertex vertex[];
} cloth;

uniform int offsetUbo;
layout(location = 4) in vec2 a_TexCoords;
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 Color;
layout(location = 2) out vec3 ViewPos;
layout(location = 3) out mat3 TBN;
layout(location = 6) flat out int imaterial;
layout(location = 7) out vec3 WorldPos;

void main()
{
    // DrawElementsInstanced uses a per-shape VBO/IBO, without baseVertex.
    // gl_VertexID therefore indexes this shape's original render vertices.
    ClothVertex deformed = cloth.vertex[gl_VertexID];
    mat4 model = objectData.ubo[offsetUbo + gl_InstanceID].ubo;
    imaterial = objectData.ubo[offsetUbo + gl_InstanceID].mat_index;
    fragTexCoord = materialData.ubm[imaterial].offset
        + a_TexCoords * materialData.ubm[imaterial].tilling;
    vec4 worldPosition = model * deformed.position;
    WorldPos = worldPosition.xyz;
    vec4 viewPosition = ubc.view * worldPosition;
    ViewPos = viewPosition.xyz;
    Color = vec3(1.0);
    mat3 tangentMatrix = mat3(model);
    vec3 N = normalize(transpose(inverse(tangentMatrix)) * deformed.normal.xyz);
    vec3 T = normalize(tangentMatrix * deformed.tangent.xyz);
    T = normalize(T - dot(T, N) * N);
    TBN = mat3(T, normalize(cross(N, T)) * deformed.tangent.w, N);
    gl_Position = ubc.proj * viewPosition;
}
