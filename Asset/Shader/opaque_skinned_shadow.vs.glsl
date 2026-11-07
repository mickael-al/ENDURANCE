#version 450

struct StructShadow
{
	mat4 projview;
	vec3 pos;
	float splitDepth;
};

layout(std430, binding = 0) buffer UniformBufferShadow
{
	StructShadow s[];
} ubs;

struct StructUBO
{
	mat4 ubo;
	int mat_index;
};

layout(std430, binding = 1) buffer UniformBufferObject 
{
	StructUBO ubo[];
}ubo;

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
} ubm;

layout(std430, binding = 4) buffer BoneCharacter
{
	mat4 bm[];
} bc;

layout(std430, binding = 5) buffer BoneCharacterData
{
	uint off;
} bcd;

uniform int offsetUbo;
uniform int offsetShadow;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangents;
layout(location = 3) in vec3 a_Color;
layout(location = 4) in vec2 a_TexCoords;

layout(location = 0) out vec2 fragTexCoord;

void decodeSkinData(vec3 packedData,out uvec4 idMatrix,out vec4 weightMatrix)
{
    uint packedIDs = floatBitsToUint(packedData.x);
    uint packedW0W1 = floatBitsToUint(packedData.y);
    uint packedW2W3 = floatBitsToUint(packedData.z);

	idMatrix = uvec4(
        packedIDs & 0xFFu,
        (packedIDs >> 8) & 0xFFu,
        (packedIDs >> 16) & 0xFFu,
        (packedIDs >> 24) & 0xFFu
    );

	weightMatrix = vec4(
        float(packedW0W1 & 0xFFFFu) / 65535.0,
        float((packedW0W1 >> 16) & 0xFFFFu) / 65535.0,
        float(packedW2W3 & 0xFFFFu) / 65535.0,
        float((packedW2W3 >> 16) & 0xFFFFu) / 65535.0
    );
}

void main()
{		
	uvec4 idMatrix;
	vec4 weightMatrix;
	decodeSkinData(a_Color, idMatrix, weightMatrix);
	weightMatrix /= max(dot(weightMatrix, vec4(1.0)), 0.00001);
	mat4 finalMatrix = bc.bm[bcd.off + idMatrix.x] * weightMatrix.x;
	finalMatrix += bc.bm[bcd.off + idMatrix.y] * weightMatrix.y;
	finalMatrix += bc.bm[bcd.off + idMatrix.z] * weightMatrix.z;
	finalMatrix += bc.bm[bcd.off + idMatrix.w] * weightMatrix.w;
	finalMatrix = ubo.ubo[offsetUbo + gl_InstanceID].ubo * finalMatrix;	

	int imaterial = ubo.ubo[offsetUbo + gl_InstanceID].mat_index;
	fragTexCoord = ubm.ubm[imaterial].offset + a_TexCoords * ubm.ubm[imaterial].tilling;
	gl_Position = ubs.s[offsetShadow].projview * finalMatrix * vec4(a_Position, 1.0);
}
