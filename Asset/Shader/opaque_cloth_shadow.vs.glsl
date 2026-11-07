#version 450

struct StructShadow { mat4 projview; vec3 pos; float splitDepth; };
layout(std430, binding = 0) buffer UniformBufferShadow
{
    StructShadow s[];
} shadowData;

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

struct ClothVertex { vec4 position; vec4 normal; vec4 tangent; };
layout(std430, binding = 6) readonly buffer DeformedCloth
{
    ClothVertex vertex[];
} cloth;

uniform int offsetUbo;
uniform int offsetShadow;
layout(location = 4) in vec2 a_TexCoords;
layout(location = 0) out vec2 fragTexCoord;

void main()
{
    int imaterial = objectData.ubo[offsetUbo + gl_InstanceID].mat_index;
    fragTexCoord = materialData.ubm[imaterial].offset
        + a_TexCoords * materialData.ubm[imaterial].tilling;
    vec4 worldPosition = objectData.ubo[offsetUbo + gl_InstanceID].ubo
        * cloth.vertex[gl_VertexID].position;
    gl_Position = shadowData.s[offsetShadow].projview * worldPosition;
}
