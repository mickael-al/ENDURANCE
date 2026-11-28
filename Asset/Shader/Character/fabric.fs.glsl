#version 450
#include "../Geometry/gbuffer_position.glsl"
#include "../Geometry/gbuffer_geometry.glsl"

// Shared by skinned trousers and the reconstructed simulated dress/rope. The
// ordinary deferred light pass provides PBR, IBL, SSAO and atmospheric lighting.
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
layout(std430, binding = 2) buffer UniformBufferMaterials { StructUBM ubm[]; } materials;
layout(binding = 0) uniform sampler2D albedoTexture;
layout(binding = 1) uniform sampler2D normalTexture;
layout(binding = 3) uniform sampler2D roughnessTexture;
layout(binding = 4) uniform sampler2D oclusionTexture;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 Color;
layout(location = 2) in vec3 ViewPos;
layout(location = 3) in mat3 TBN;
layout(location = 6) flat in int imaterial;
layout(location = 0) out vec2 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gColor;
layout(location = 3) out vec3 gOther;

void main()
{
    float geometryTag = geGBufferGeometryTag(dFdx(ViewPos), dFdy(ViewPos));
    vec4 texel = texture(albedoTexture, fragTexCoord);
    if (texel.a < 0.5) discard;
    StructUBM material = materials.ubm[imaterial];
    vec3 mapped = texture(normalTexture, fragTexCoord).xyz * 2.0 - 1.0;
    mapped = mix(vec3(0.0, 0.0, 1.0), mapped, clamp(material.normal, 0.0, 1.0));
    vec3 N = TBN * mapped;
    gNormal = N * inversesqrt(max(dot(N, N), 1e-12));
    // Both visible sides of loose woven cloth need outward-facing lighting.
    if (!gl_FrontFacing) gNormal = -gNormal;
    gColor = vec4(clamp(material.albedo.rgb * texel.rgb, 0.0, 1.0), 1.0);
    gOther = vec3(0.0,
        clamp(material.roughness * texture(roughnessTexture, fragTexCoord).r, 0.55, 1.0),
        clamp(material.ao * texture(oclusionTexture, fragTexCoord).r, 0.0, 1.0));
    gPosition = geGBufferPackPosition(ViewPos, geometryTag);
}
