#version 450
#include "../Atmosphere/lighting.glsl"
#include "../Shadow/spot_surface.glsl"

// Dense dielectric stone, not a transparent surface. Use the existing forward
// skinned vertex shader (bone buffers 6/7), depth writes and alpha=1. The alpha
// test matches shadow.fs.glsl, including the separately authored face layers.
#ifndef GE_CHARACTER_SCATTER
#define GE_CHARACTER_SCATTER 0.18
#endif
#ifndef GE_CHARACTER_FACETS
#define GE_CHARACTER_FACETS 0.08
#endif

const float PI = 3.141592653589793;
const vec3 DIELECTRIC_F0 = vec3(0.0478515625); // ((1.56 - 1)/(1.56 + 1))^2

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
layout(std430, binding = 2) buffer UniformBufferMaterials { StructUBM ubm[]; } ubm;
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
    int minPlaneID;
    int maxPlaneID;
    float shadowDepthBias;
    float shadowNormalBias;
};
layout(std430, binding = 4) buffer UniformBufferLight { StructUBL ubl[]; } ubl;
struct StructShadow { mat4 projview; vec3 pos; float splitDepth; };
layout(std430, binding = 5) buffer UniformBufferShadow { StructShadow s[]; } ubs;

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
layout(location = 0) out vec4 o_FragColor;

const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.5, 0.5, 0.5, 1.0);

vec3 safeNormalize(vec3 value, vec3 fallback)
{
    float squared = dot(value, value);
    return squared > 1e-16 ? value * inversesqrt(squared) : fallback;
}

vec3 fresnel(float cosine)
{
    float f = 1.0 - clamp(cosine, 0.0, 1.0);
    float f2 = f * f;
    return DIELECTRIC_F0 + (1.0 - DIELECTRIC_F0) * (f2 * f2 * f);
}

// The broad diffuse lobe approximates short-distance multiple scattering in
// dense stone. Its spherical integral equals the Lambert lobe's: it redistributes
// incoming energy, rather than adding an unlit rim or emissive fake translucency.
vec3 directBRDF(vec3 albedo, vec3 N, vec3 V, vec3 L, float roughness, float specularWeight)
{
    float nDotL = dot(N, L);
    float nDotV = max(dot(N, V), 0.001);
    vec3 H = safeNormalize(V + L, N);
    float nDotH = max(dot(N, H), 0.0);
    vec3 F = fresnel(max(dot(H, V), 0.0));
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / max(PI * denominator * denominator, 1e-7);
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float nl = max(nDotL, 0.0);
    float geometry = (nDotV / (nDotV * (1.0 - k) + k))
                   * (nl / (nl * (1.0 - k) + k));
    vec3 specular = F * distribution * geometry / max(4.0 * nDotV * nl, 0.0001);
    const float wrap = 0.45;
    float broadDiffuse = max(nDotL + wrap, 0.0) / ((1.0 + wrap) * (1.0 + wrap));
    float diffuse = mix(nl, broadDiffuse, clamp(float(GE_CHARACTER_SCATTER), 0.0, 1.0));
    return (1.0 - F) * albedo * (diffuse / PI) + specular * nl * specularWeight;
}

float pointShadow(vec3 N, vec3 L, int firstLayer)
{
    float visibility = 1.0;
    vec2 texel = 1.0 / vec2(textureSize(shadowDepth, 0).xy);
    float cosine = clamp(dot(N, L), 0.0, 1.0);
    float bias = min(0.005 * sqrt(max(1.0 - cosine * cosine, 0.0)) / max(cosine, 0.1), 0.01);
    for (int face = 0; face < 6; ++face)
    {
        vec4 coord = biasMat * ubs.s[firstLayer + face].projview * vec4(WorldPos, 1.0);
        if (!(coord.w > 0.0)) continue;
        coord /= coord.w;
        if (any(lessThan(coord.xyz, vec3(0.0))) || any(greaterThan(coord.xyz, vec3(1.0)))) continue;
        float sum = 0.0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                vec2 uv = coord.xy + vec2(x, y) * texel;
                bool outside = any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)));
                sum += outside || texture(shadowDepth, vec3(uv, firstLayer + face)).r >= coord.z - bias ? 1.0 : 0.0;
            }
        visibility = min(visibility, sum / 9.0);
    }
    return visibility;
}

float directionalShadow(int lightIndex, float viewDepth, vec3 worldDx, vec3 worldDy, vec3 error)
{
    int firstLayer = ubl.ubl[lightIndex].shadowID;
    if (firstLayer < 0) return 1.0;
    int cascade = 0;
    for (int k = 0; k < 3; ++k)
        if (viewDepth < ubs.s[firstLayer + k].splitDepth) cascade = k + 1;
    int layer = firstLayer + cascade;
    vec2 bias = vec2(ubl.ubl[lightIndex].shadowDepthBias, ubl.ubl[lightIndex].shadowNormalBias);
    vec4 coord = biasMat * ubs.s[layer].projview * vec4(WorldPos, 1.0);
    float visibility = geDirectionalSurfaceShadow(shadowDepth, coord, layer, ubs.s[layer].projview,
                                                  worldDx, worldDy, error, bias);
    if (cascade < 3)
    {
        float previous = cascade > 0 ? ubs.s[layer - 1].splitDepth : 0.0;
        float blend = geShadowCascadeBlend(viewDepth, previous, ubs.s[layer].splitDepth);
        if (blend > 0.0)
        {
            vec4 next = biasMat * ubs.s[layer + 1].projview * vec4(WorldPos, 1.0);
            float other = geDirectionalSurfaceShadowFromNormal(shadowDepth, next, layer + 1,
                ubs.s[layer + 1].projview, geDirectionalSurfaceNormal(worldDx, worldDy), error, bias);
            visibility = mix(visibility, other, blend);
        }
    }
    return visibility;
}

void main()
{
    // Derivatives precede alpha discard and light branches (quad correctness).
    vec3 worldDx = dFdx(WorldPos), worldDy = dFdy(WorldPos);
    vec3 error = 4.0 * 1.1920928955078125e-7 * abs(WorldPos);
    vec4 sampleColor = texture(albedoTexture, fragTexCoord);
    if (sampleColor.a < 0.5) discard;

    StructUBM material = ubm.ubm[imaterial];
    // Engine textures are uploaded as linear-format RGBA8, so match the existing
    // lighting pass's one-and-only gamma decode. Never decode normal/roughness.
    vec3 albedo = pow(clamp(sampleColor.rgb * material.albedo.rgb, 0.0, 1.0), vec3(ubd.gamma));
    vec3 tangentNormal = mix(vec3(0.0, 0.0, 1.0), texture(normalTexture, fragTexCoord).rgb * 2.0 - 1.0,
                            clamp(material.normal, 0.0, 1.0));
    vec3 smoothNormal = safeNormalize(TBN[2], vec3(0.0, 1.0, 0.0));
    vec3 N = safeNormalize(TBN * tangentNormal, smoothNormal);
    vec3 geometricNormal = safeNormalize(cross(worldDx, worldDy), smoothNormal);
    if (dot(geometricNormal, N) < 0.0) geometricNormal = -geometricNormal;
    N = safeNormalize(mix(N, geometricNormal, clamp(float(GE_CHARACTER_FACETS), 0.0, 0.3)), N);
    if (!gl_FrontFacing) N = -N;
    vec3 V = safeNormalize(ubc.camPos - WorldPos, N);
    float roughness = clamp(material.roughness * texture(roughnessTexture, fragTexCoord).r, 0.16, 0.95);
    float ao = clamp(material.ao * texture(oclusionTexture, fragTexCoord).r, 0.0, 1.0);
    float viewDepth = (ubc.view * vec4(WorldPos, 1.0)).z;
    vec3 direct = vec3(0.0);

    for (int i = 0; i < ubd.maxLight; ++i)
    {
        if (!geAtmosphereLightEnabled(i)) continue;
        StructUBL light = ubl.ubl[i];
        vec3 delta = light.position - WorldPos;
        float distanceSquared = max(dot(delta, delta), 0.0001);
        vec3 L = safeNormalize(delta, N);
        vec3 radiance = light.color * max(light.range, 0.0) / distanceSquared;
        float shadow = 1.0;
        if (light.status == 0)
        {
            L = safeNormalize(-light.direction, N);
            radiance = geAtmosphereDirect(light.color * (max(light.range, 0.0) / 10.0), i, WorldPos, L);
            shadow = directionalShadow(i, viewDepth, worldDx, worldDy, error);
            shadow = geAtmosphereShadow(i, shadow, ubd.ambiant);
        }
        else if (light.status == 1)
        {
            if (light.shadowID >= 0) shadow = pointShadow(N, L, light.shadowID);
            shadow = mix(ubd.ambiant, 1.0, shadow);
        }
        else if (light.status == 2)
        {
            float halfAngle = radians(clamp(light.spotAngle, 0.1, 179.0)) * 0.5;
            float spot = dot(safeNormalize(light.direction, -L), -L);
            float factor = smoothstep(cos(halfAngle), cos(halfAngle - min(radians(4.0), halfAngle)), spot);
            radiance *= factor * factor;
            if (light.shadowID >= 0)
                shadow = geSpotSurfaceShadow(shadowDepth, ubs.s[light.shadowID].projview, light.shadowID,
                    WorldPos, light.position, geDirectionalSurfaceNormal(worldDx, worldDy), error,
                    vec2(light.shadowDepthBias, light.shadowNormalBias));
            shadow = mix(ubd.ambiant, 1.0, shadow);
        }
        else continue;
        direct += directBRDF(albedo, N, V, L, roughness, geAtmosphereDirectSpecular(i))
                * max(radiance, vec3(0.0)) * shadow;
    }

    float nDotV = max(dot(N, V), 0.0);
    float grazing = 1.0 - clamp(nDotV, 0.0, 1.0);
    vec3 F = DIELECTRIC_F0 + (max(vec3(1.0 - roughness), DIELECTRIC_F0) - DIELECTRIC_F0)
           * grazing * grazing * grazing * grazing * grazing;
    vec3 irradiance = geAtmosphereIbl(texture(irradianceMap, N).rgb);
    // A bounded fraction of the opposite hemisphere's irradiance approximates
    // the diffuse volume; no environment/background refraction is required.
    if (float(GE_CHARACTER_SCATTER) > 0.0)
    {
        vec3 opposite = geAtmosphereIbl(texture(irradianceMap, -N).rgb);
        irradiance = mix(irradiance, opposite, clamp(float(GE_CHARACTER_SCATTER), 0.0, 1.0) * 0.15);
    }
    vec3 reflected = reflect(-V, N);
    vec3 environment = geAtmosphereIbl(textureLod(prefilterMap, reflected, roughness * 4.0).rgb);
    vec2 brdf = texture(brdfLUT, vec2(nDotV, roughness)).rg;
    vec3 ambient = ((1.0 - F) * irradiance * albedo * max(ubd.ambiant, 0.0)
                  + environment * (F * brdf.x + brdf.y)) * ao;
    vec3 color = max(ambient + direct, vec3(0.0));
    o_FragColor = vec4(geAtmComposite(color, WorldPos, gl_FragCoord.xy * uAtmosphereInvResolution), 1.0);
}
