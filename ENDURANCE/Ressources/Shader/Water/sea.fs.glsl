#version 460 core
// ENDURANCE sea variant of Asset/Shader/water_transparency.fs.glsl.
// Tiles have no coastal mask; opaque depth handles intersections with land.
#include "../../../../Asset/Shader/Atmosphere/lighting.glsl"
#include "../../../../Asset/Shader/Shadow/spot_surface.glsl"
#include "sea_noise.glsl"

layout(early_fragment_tests) in;

#define SHADOW_MAP_CASCADE_COUNT 4
#define SHADOW_MAP_CUBE_COUNT 6

const float PI = 3.14159265359;

layout(std430, binding = 0) readonly buffer UniformBufferCamera
{
	vec3 camPos;
	mat4 view;
	mat4 invView;
	mat4 proj;
} ubc;

layout(std430, binding = 3) readonly buffer UniformBufferDivers
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
	float shadowDepthBias; // texels; occupies existing std430 padding
	float shadowNormalBias; // texels; light stride remains 80 bytes
};

layout(std430, binding = 4) readonly buffer UniformBufferLight
{
	StructUBL ubl[];
} ubl;

struct StructShadow
{
	mat4 projview;
	vec3 pos;
	float splitDepth;
};

layout(std430, binding = 5) readonly buffer UniformBufferShadow
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
	// Normalized dot products can round slightly above one (camera at light).
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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

// Existing opaque RG32F position attachment: signed view-space Z and sky tag.
// It is separate from the currently attached hardware depth texture.
layout(binding=4) uniform sampler2D opaquePosition;
layout(binding = 5) uniform sampler2DArray shadowDepth;
layout(binding = 6) uniform samplerCube irradianceMap;
layout(binding = 7) uniform samplerCube prefilterMap;
layout(binding = 8) uniform sampler2D brdfLUT;

layout(location=0) in vec3 WorldPos;
layout(location=2) in float SeaViewZ;

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

out vec4 o_FragColor;

void main()
{
	// Surface geometry, before any material/light/cascade branch or normal mapping.
	vec3 shadowWorldDx = dFdx(WorldPos);
	vec3 shadowWorldDy = dFdy(WorldPos);
	// WorldPos is interpolated directly; no G-buffer view-position reconstruction.
	vec3 shadowPositionError = 4.0 * 1.1920928955078125e-7 * abs(WorldPos);

    // Fixed game-owned art settings, expressed in linear HDR color and metres.
    const vec3 shallowColor=vec3(0.025,0.24,0.27);
    const vec3 deepColor=vec3(0.006,0.025,0.045);
    const vec3 foamColor=vec3(0.66,0.72,0.71);
    const float deepDistance=36.0;
    const float deepOpacity=0.85; // retain a faint view of an existing seabed
    const float foamDistance=0.9;
    const float foamFrequency=0.60;
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy),ivec2(0),textureSize(opaquePosition,0)-1);
    vec2 opaque=texelFetch(opaquePosition,pixel,0).rg;
    bool hasOpaque=opaque.x<0.0 && opaque.y!=-2.0;
    // View Z is already linear; no near/far inversion and no extra depth copy.
    float depth=hasOpaque?max(SeaViewZ-opaque.x,0.0):deepDistance;
    float depthWidth=abs(dFdx(depth))+abs(dFdy(depth));
    float footprint=max(length(shadowWorldDx.xz),length(shadowWorldDy.xz));
    vec2 slope=seaRippleSlope(WorldPos.xz,ubd.u_time,footprint);
    vec3 n=normalize(vec3(-slope.x,1.0,-slope.y));
    // A visible underside is useful while traversing the marine biome. This is
    // surface lighting only; underwater fog/refraction are not introduced here.
    if(!gl_FrontFacing) n=-n;
    float foam=0.0;
    if(hasOpaque && depth<foamDistance*1.5)
    {
        float fade=1.0-smoothstep(0.20,0.80,footprint*foamFrequency);
        float pattern=0.5;
        if(fade>0.0001)
            pattern+=seaNoiseValueGradient(WorldPos.xz*foamFrequency
                +ubd.u_time*vec2(-0.08,0.06)).x*fade;
        float edge=depth/foamDistance;
        float width=max(0.12,depthWidth/foamDistance);
        foam=1.0-smoothstep(pattern-width,pattern+width,edge);
    }
    float deepBlend=clamp(depth/deepDistance,0.0,1.0);
    vec4 col=vec4(mix(mix(shallowColor,deepColor,deepBlend),foamColor,foam),
        mix(mix(0.50,hasOpaque?deepOpacity:1.0,deepBlend),0.98,foam));
    vec4 normal=vec4(n,0.0);
    vec3 other=vec3(0.0,mix(0.20,0.72,foam),1.0);
    vec3 color=col.rgb;

	vec3 viewDelta=ubc.camPos-WorldPos;
    vec3 V=viewDelta*inversesqrt(max(dot(viewDelta,viewDelta),1e-12));
	float ab =  mix(1.0, ubd.ambiant, col.a);

	vec3 F0 = vec3(0.02);
	F0 = mix(F0, color, other.x);
	if (normal.a <= 0)
	{
		// Reflectance equation
		vec3 Lo = vec3(0.0);
		for (int i = 0; i < ubd.maxLight; i++)
		{
			float shadow = 1.0;
			vec3 L;
            vec3 radiance=vec3(0.0);
			if (ubl.ubl[i].status == 0)
			{
				L = normalize(-ubl.ubl[i].direction);
			}
            else
            {
                vec3 lightDelta=ubl.ubl[i].position-WorldPos;
                float distanceSquared=dot(lightDelta,lightDelta);
                L=lightDelta*inversesqrt(max(distanceSquared,1e-12));
                radiance=ubl.ubl[i].color*(ubl.ubl[i].range/max(distanceSquared,1e-6));
            }
            float NdotL=max(dot(normal.rgb,L),0.0);
            if (NdotL<=0.0) continue;
			vec3 halfVector = V + L;
			vec3 H = halfVector * inversesqrt(max(dot(halfVector,halfVector),1e-12));

			// Cook-Torrance BRDF
			float NDF = DistributionGGX(normal.rgb, H, other.y);
			float G = GeometrySmith(normal.rgb, V, L, other.y);
			vec3 F = fresnelSchlick(max(dot(H, V), 0.001), F0);

			vec3 kS = F;
			vec3 kD = vec3(1.0) - kS;
			kD *= 1.0 - other.x;

			vec3 numerator = NDF * G * F;
			float denominator = 4.0 * max(dot(normal.rgb, V), 0.0) * NdotL + 0.0001;
			vec3 specular = (numerator / denominator) * geAtmosphereDirectSpecular(i);

			// Add to outgoing radiance Lo
			if (ubl.ubl[i].status == 0) // DirLight
			{
				if (ubl.ubl[i].shadowID >= 0)
				{
					uint cascadeIndex = 0;
					for (uint k = 0; k < SHADOW_MAP_CASCADE_COUNT - 1; ++k)
					{
						if (SeaViewZ < ubs.s[ubl.ubl[i].shadowID + k].splitDepth)
						{
							cascadeIndex = k + 1;
						}
					}
					vec4 shadowCoord = (biasMat * ubs.s[ubl.ubl[i].shadowID + cascadeIndex].projview) * vec4(WorldPos, 1.0);
					shadow = geDirectionalSurfaceShadow(shadowDepth, shadowCoord,
						int(ubl.ubl[i].shadowID + cascadeIndex), ubs.s[ubl.ubl[i].shadowID + cascadeIndex].projview,
						shadowWorldDx, shadowWorldDy, shadowPositionError,
						vec2(ubl.ubl[i].shadowDepthBias, ubl.ubl[i].shadowNormalBias));
					if (cascadeIndex + 1 < SHADOW_MAP_CASCADE_COUNT)
					{
						uint layer = ubl.ubl[i].shadowID + cascadeIndex;
						float previousSplit = cascadeIndex > 0 ? ubs.s[layer - 1].splitDepth : 0.0;
						float blend = geShadowCascadeBlend(SeaViewZ, previousSplit, ubs.s[layer].splitDepth);
						if (blend > 0.0)
						{
							vec4 nextCoord = (biasMat * ubs.s[layer + 1].projview) * vec4(WorldPos, 1.0);
							float nextShadow = geDirectionalSurfaceShadowFromNormal(shadowDepth, nextCoord,
								int(layer + 1), ubs.s[layer + 1].projview, geDirectionalSurfaceNormal(shadowWorldDx, shadowWorldDy), shadowPositionError,
								vec2(ubl.ubl[i].shadowDepthBias, ubl.ubl[i].shadowNormalBias));
							shadow = mix(shadow, nextShadow, blend);
						}
					}
				}
				Lo += ((kD * color / PI + specular) * geAtmosphereDirect(ubl.ubl[i].color * (ubl.ubl[i].range / 10.0), i, WorldPos, L) * NdotL) * geAtmosphereShadow(i, shadow, ab);
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
					if (ubl.ubl[i].shadowID >= 0)
					{
						shadow = geSpotSurfaceShadow(shadowDepth, ubs.s[ubl.ubl[i].shadowID].projview,
							ubl.ubl[i].shadowID, WorldPos, ubl.ubl[i].position,
							geDirectionalSurfaceNormal(shadowWorldDx, shadowWorldDy), shadowPositionError,
							vec2(ubl.ubl[i].shadowDepthBias, ubl.ubl[i].shadowNormalBias));
					}
					float transitionAngle = min(radians(4.0), spotAngle * 0.5);
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

		vec3 irradiance = geAtmosphereIbl(texture(irradianceMap, normal.rgb).rgb);
		vec3 diffuse = vec3(ab) * irradiance * color;

		// sample both the pre-filter map and the BRDF lut and combine them together as per the Split-Sum approximation to get the IBL specular part.
		const float MAX_REFLECTION_LOD = 4.0;
		vec3 prefilteredColor = geAtmosphereIbl(textureLod(prefilterMap, R, other.y * MAX_REFLECTION_LOD).rgb);
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

	o_FragColor = vec4(geAtmComposite(color, WorldPos, gl_FragCoord.xy * uAtmosphereInvResolution),col.a);
}
