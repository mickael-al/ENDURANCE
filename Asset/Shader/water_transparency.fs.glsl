#version 450

#define SHADOW_MAP_CASCADE_COUNT 4
#define SHADOW_MAP_CUBE_COUNT 6

const float PI = 3.14159265359;

layout(std430, binding = 0) buffer UniformBufferCamera
{
	vec3 camPos;
	mat4 view;
	mat4 invView;
	mat4 proj;
} ubc;

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

struct StructUBL
{
	vec3 position;
	vec3 color;
	vec3 direction;
	float range;
	float spotAngle;
	int status;
	int shadowID;
	int minPlaneID;//default -1
	int maxPlaneID;//default -1
};

layout(std430, binding = 4) buffer UniformBufferLight
{
	StructUBL ubl[];
} ubl;

struct StructShadow
{
	mat4 projview;
	vec3 pos;
	float splitDepth;
};

layout(std430, binding = 5) buffer UniformBufferShadow
{
	StructShadow s[];
} ubs;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float num = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

const mat4 biasMat = mat4(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 0.5, 0.0,
	0.5, 0.5, 0.5, 1.0
);

layout(binding = 0) uniform sampler2D albedoTexture;
layout(binding = 1) uniform sampler2D normalTexture;
layout(binding = 2) uniform sampler2D metallicTexture;
layout(binding = 3) uniform sampler2D roughnessTexture;
layout(binding = 4) uniform sampler2D oclusionTexture;
layout(binding = 5) uniform sampler2DArray shadowDepth;
layout(binding = 6) uniform samplerCube irradianceMap;
layout(binding = 7) uniform samplerCube prefilterMap;
layout(binding = 8) uniform sampler2D brdfLUT;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 Color;
layout(location = 2) in vec3 WorldPos;
layout(location = 3) in mat3 TBN;
layout(location = 6) flat in int imaterial;

float calculateBias(vec4 shadowCoord, vec3 normal, vec3 lightDir)
{
	// Finite slope bias, including grazing incidence and slight dot overshoot.
	float cosine = clamp(dot(normal, lightDir), 0.0, 1.0);
	float slope = sqrt(max(1.0 - cosine * cosine, 0.0)) / max(cosine, 0.1);
	return min(0.005 * slope, 0.01);
}

float textureProj(vec4 shadowCoord, vec2 offset, uint cascadeIndex, float bias)
{
	vec2 uv = shadowCoord.xy + offset;
	// biasMat has already mapped clip coordinates to [0,1]. Outside taps
	// are lit; clamping them to the edge would repeat unrelated occluders.
	if (shadowCoord.z < 0.0 || shadowCoord.z > 1.0 ||
		any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
		return 1.0;
	float depth = texture(shadowDepth, vec3(uv, cascadeIndex)).r;
	return depth < shadowCoord.z - bias ? 0.0 : 1.0;
}

float filterPCF(vec4 sc, uint cascadeIndex, float bias)
{
	// Keep homogeneous W until this check: dividing at the call site loses
	// the sign needed to reject receivers behind perspective shadow cameras.
	if (!(sc.w > 0.0)) return 1.0;
	sc /= sc.w;
	if (any(lessThan(sc.xyz, vec3(0.0))) || any(greaterThan(sc.xyz, vec3(1.0))))
		return 1.0;
	vec2 texel = 1.0 / vec2(textureSize(shadowDepth, 0).xy);
	float shadowFactor = 0.0;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y)
			shadowFactor += textureProj(sc, vec2(x, y) * texel, cascadeIndex, bias);
	return shadowFactor / 9.0;
}

vec2 gradientNoise_dir(vec2 p)
{
	p = mod(p, 289.0);
	float x = mod((34.0 * p.x + 1.0) * p.x, 289.0) + p.y;
	x = mod((34.0 * x + 1.0) * x, 289.0);
	x = fract(x / 41.0) * 2.0 - 1.0;
	return normalize(vec2(x - floor(x + 0.5), abs(x) - 0.5));
}

float gradientNoise(vec2 p)
{
	vec2 ip = floor(p);
	vec2 fp = fract(p);
	float d00 = dot(gradientNoise_dir(ip), fp);
	float d01 = dot(gradientNoise_dir(ip + vec2(0.0, 1.0)), fp - vec2(0.0, 1.0));
	float d10 = dot(gradientNoise_dir(ip + vec2(1.0, 0.0)), fp - vec2(1.0, 0.0));
	float d11 = dot(gradientNoise_dir(ip + vec2(1.0, 1.0)), fp - vec2(1.0, 1.0));
	fp = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);
	return mix(mix(d00, d01, fp.y), mix(d10, d11, fp.y), fp.x);
}

float GradientNoise_float(vec2 UV, float Scale)
{
	return gradientNoise(UV * Scale) + 0.5;
}

float linearizeDepth(float depth)
{
	float z = depth * 2.0 - 1.0; // Remap to NDC space (-1, 1)
	float near = ubc.proj[3][2] / (ubc.proj[2][2] - 1.0); // Par exemple
	float far = ubc.proj[3][2] / (ubc.proj[2][2] + 1.0); // Par exemple
	return (2.0 * near * far) / (far + near - z * (far - near));
}

vec3 computeNormal(vec2 uv, float scale, float strength)
{
	float eps = 0.001; // petit offset pour d�riv�e

	// Hauteur au point courant
	float h = gradientNoise(uv * scale);

	// D�riv�es partielles (diff�rence finie)
	float dx = gradientNoise((uv + vec2(eps, 0.0)) * scale) - h;
	float dy = gradientNoise((uv + vec2(0.0, eps)) * scale) - h;

	// Normal non normalis�e (z = 1.0 pour repr�senter "vers l'avant")
	vec3 n = normalize(vec3(dx * strength, dy * strength, 1.0));

	// Remap de [-1,1] -> [0,1] pour stockage en texture
	return n * 0.5 + 0.5;
}


out vec4 o_FragColor;

void main()
{	
	ivec2 viewportSize = textureSize(oclusionTexture, 0);
	float depth = texture(oclusionTexture, gl_FragCoord.xy / viewportSize).r;
	float WaterDepth = 5.0;
	float FoamAmount = 0.8;
	float FoamOutOff = 1.0;
	float FoamSpeed = 1.0;
	float FoamScale = 256.0;
	float RefractionSpeed = 0.02;
	float RefractionScale = 5.5;
	float RefractioStrength = 5.0;
	vec4 shallow = vec4(0.06541473, 0.8400926, 0.9245283, 0.7f);
	vec4 deepWater = vec4(0.0, 0.1013241, 0.745283,1.0f);
	vec4 foamColor = vec4(1.0, 1.0, 1.0,1.0);
	depth = linearizeDepth(depth) - linearizeDepth(gl_FragCoord.z);
	vec4 wc = mix(shallow, deepWater, clamp(depth / WaterDepth,0,1));
	vec2 fm = vec2(FoamSpeed * ubd.u_time) + fragTexCoord * vec2(FoamScale);
	vec2 rm = vec2(RefractionSpeed * ubd.u_time) + fragTexCoord * vec2(RefractionScale);
	vec3 norm = computeNormal(rm, 20.0f, RefractioStrength);
	float s = step((depth / FoamAmount) * FoamOutOff, GradientNoise_float(fm, 1.0));
	wc = mix(wc, foamColor, s);

	vec4 col = ubm.ubm[imaterial].albedo * vec4(Color, 1.0) * wc;
	norm = mix(vec3(0.5, 0.5, 1.0), norm, ubm.ubm[imaterial].normal);
	norm = normalize(norm * 2.0 - 1.0);
	norm = normalize(TBN * norm);
	vec2 aoe = vec2(1,0);
	vec4 normal = vec4(norm, ubm.ubm[imaterial].emit * aoe.y);
	vec3 other = vec3(ubm.ubm[imaterial].metallic,
		mix(ubm.ubm[imaterial].roughness,1,s),
		ubm.ubm[imaterial].ao * aoe.x);

	vec3 viewPos = (ubc.view * vec4(WorldPos,1.0)).xyz;
	vec3 color = col.rgb;
	color = pow(color, vec3(ubd.gamma));

	vec3 V = normalize(ubc.camPos - WorldPos);
	float ab =  mix(1.0, ubd.ambiant, col.a);

	vec3 F0 = vec3(0.04);
	F0 = mix(F0, color, other.x);
	if (normal.a <= 0)
	{
		// Reflectance equation
		vec3 Lo = vec3(0.0);
		for (int i = 0; i < ubd.maxLight; i++)
		{
			float shadow = 1.0;
			vec3 L = normalize(ubl.ubl[i].position - WorldPos);
			float distance = length(ubl.ubl[i].position - WorldPos);
			float attenuation = 1.0 / (distance * distance);
			vec3 radiance = ubl.ubl[i].color * attenuation * ubl.ubl[i].range;
			if (ubl.ubl[i].status == 0)
			{
				L = normalize(-ubl.ubl[i].direction);
			}
			vec3 H = normalize(V + L);

			// Cook-Torrance BRDF
			float NDF = DistributionGGX(normal.rgb, H, other.y);
			float G = GeometrySmith(normal.rgb, V, L, other.y);
			vec3 F = fresnelSchlick(max(dot(H, V), 0.001), F0);

			vec3 kS = F;
			vec3 kD = vec3(1.0) - kS;
			kD *= 1.0 - other.x;

			vec3 numerator = NDF * G * F;
			float denominator = 4.0 * max(dot(normal.rgb, V), 0.0) * max(dot(normal.rgb, L), 0.0) + 0.0001;
			vec3 specular = (numerator / denominator);

			// Add to outgoing radiance Lo
			float NdotL = max(dot(normal.rgb, L), 0.0);

			if (ubl.ubl[i].status == 0) // DirLight
			{
				if (ubl.ubl[i].shadowID >= 0)
				{
					uint cascadeIndex = 0;
					for (uint k = 0; k < SHADOW_MAP_CASCADE_COUNT - 1; ++k)
					{
						if (viewPos.z < ubs.s[ubl.ubl[i].shadowID + k].splitDepth)
						{
							cascadeIndex = k + 1;
						}
					}
					vec4 shadowCoord = (biasMat * ubs.s[ubl.ubl[i].shadowID + cascadeIndex].projview) * vec4(WorldPos, 1.0);
					float bias = calculateBias(shadowCoord, normal.rgb, L);
					shadow = filterPCF(shadowCoord, ubl.ubl[i].shadowID + cascadeIndex, bias);
				}
				Lo += ((kD * color / PI + specular) * ubl.ubl[i].color * (ubl.ubl[i].range / 10.0) * NdotL) * mix(ab, 1.0, shadow);
			}
			else if (ubl.ubl[i].status == 1) // PointLight
			{
				if (ubl.ubl[i].shadowID >= 0)
				{
					for (uint k = 0; k < SHADOW_MAP_CUBE_COUNT && shadow > 0.5f; ++k)
					{
						vec4 shadowCoord = (biasMat * ubs.s[ubl.ubl[i].shadowID + k].projview) * vec4(WorldPos, 1.0);
						float bias = calculateBias(shadowCoord, normal.rgb, L);
						shadow *= filterPCF(shadowCoord, ubl.ubl[i].shadowID + k, bias);
					}
				}
				Lo += (kD * color / PI + specular) * radiance * NdotL * mix(ab,1.0, shadow);
			}
			else if (ubl.ubl[i].status == 2) // SpotLight
			{
				vec3 lightDir = normalize(ubl.ubl[i].direction);
				float spotAngle = radians(ubl.ubl[i].spotAngle);
				float spotEffect = dot(lightDir, -L);

				if (spotEffect > cos(spotAngle / 2.0))
				{
					float transitionAngle = radians(4.0);
					float edge0 = cos(spotAngle / 2.0 - transitionAngle);
					float edge1 = cos(spotAngle / 2.0);
					float smoothFactor = smoothstep(edge1, edge0, spotEffect);

					Lo += (kD * color / PI + specular) * radiance * NdotL * pow(smoothFactor, 2.0) * mix(ab, 1.0, shadow);
				}
			}
		}
		//IBL PBR
		vec3 R = reflect(-V, normal.rgb);
		vec3 F = fresnelSchlickRoughness(max(dot(normal.rgb, V), 0.0), F0, other.y);

		vec3 kS = F;
		vec3 kD = 1.0 - kS;
		kD *= 1.0 - other.x;

		vec3 irradiance = texture(irradianceMap, normal.rgb).rgb;
		vec3 diffuse = vec3(ab) * irradiance * color;

		// sample both the pre-filter map and the BRDF lut and combine them together as per the Split-Sum approximation to get the IBL specular part.
		const float MAX_REFLECTION_LOD = 4.0;
		vec3 prefilteredColor = textureLod(prefilterMap, R, other.y * MAX_REFLECTION_LOD).rgb;
		vec2 brdf = texture(brdfLUT, vec2(max(dot(normal.rgb, V), 0.0), other.y)).rg;
		vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);
		//IBL PBR

		vec3 ambient = (kD * diffuse + specular) * other.z;

		color = ambient + Lo;
	}
	else
	{
		color = normal.a * color;
	}
	
	o_FragColor = vec4(color,col.a);
}