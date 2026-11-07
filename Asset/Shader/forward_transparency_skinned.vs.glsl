#version 450

layout(std430, binding = 0) buffer UniformBufferCamera
{
	vec3 camPos;
	mat4 view;
	mat4 invView;
	mat4 proj;
} ubc;

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

layout(std430, binding = 3) buffer UniformBufferDivers
{
	int maxLight;
	float u_time;
	float gamma;
	float ambiant;
	float fov;
	bool ortho;
} ubd;

layout(std430, binding = 6) buffer BoneCharacter
{
	mat4 bm[];
} bc;

layout(std430, binding = 7) buffer BoneCharacterData
{
	uint off;
} bcd;

uniform int offsetUbo;

in vec3 a_Position;
in vec3 a_Normal;
in vec3 a_Tangents;
in vec3 a_Color;
in vec2 a_TexCoords;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 Color;
layout(location = 2) out vec3 WorldPos;
layout(location = 3) out mat3 TBN;
layout(location = 6) out flat int imaterial;

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

	imaterial = ubo.ubo[offsetUbo + gl_InstanceID].mat_index;
	fragTexCoord = ubm.ubm[imaterial].offset + a_TexCoords * ubm.ubm[imaterial].tilling;
	vec4 wp = finalMatrix * vec4(a_Position, 1.0);
	WorldPos = vec3(wp);
	Color = vec3(1.0);
	mat3 tangentMatrix = mat3(finalMatrix);
	mat3 normalMatrix = transpose(inverse(tangentMatrix));
	vec3 T = normalize(tangentMatrix * a_Tangents);
	vec3 N = normalize(normalMatrix * a_Normal);
	T = normalize(T - dot(T, N) * N);
	vec3 B = normalize(cross(N, T));
	TBN = mat3(T, B, N);
	gl_Position = ubc.proj * ubc.view * wp;
}
