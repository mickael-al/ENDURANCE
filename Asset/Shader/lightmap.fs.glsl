#version 450

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

layout(binding = 0) uniform sampler2D albedoTexture;
layout(binding = 1) uniform sampler2D normalTexture;
layout(binding = 2) uniform sampler2D metallicTexture;
layout(binding = 3) uniform sampler2D roughnessTexture;
// For this pipeline only, the existing occlusion slot carries the baked lightmap.
layout(binding = 4) uniform sampler2D oclusionTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec2 fragLightMapCoord;
layout(location = 2) in vec3 ViewPos;
layout(location = 3) in mat3 TBN;
layout(location = 6) flat in int imaterial;

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gColor;
layout(location = 3) out vec3 gOther;

void main(void)
{
	vec4 col = texture(albedoTexture, fragTexCoord);
	if (col.a < 0.5)
	{
		discard;
	}

	vec3 normal = texture(normalTexture, fragTexCoord).rgb;
	normal = mix(vec3(0.5, 0.5, 1.0), normal, ubm.ubm[imaterial].normal);
	normal = normalize(normal * 2.0 - 1.0);
	gNormal = normalize(TBN * normal);

	float bakedOcclusion = texture(oclusionTexture, clamp(fragLightMapCoord, 0.0, 1.0)).r;
	gColor = ubm.ubm[imaterial].albedo * col;
	gOther.r = ubm.ubm[imaterial].metallic * texture(metallicTexture, fragTexCoord).r;
	gOther.g = ubm.ubm[imaterial].roughness * texture(roughnessTexture, fragTexCoord).r;
	gOther.b = ubm.ubm[imaterial].ao * bakedOcclusion;
	gPosition = vec4(ViewPos, ubm.ubm[imaterial].emit);
}
