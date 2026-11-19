// Perspective spotlight shadows share comparison filtering with the sun,
// but their world-space texel footprint grows with receiver distance.
#ifndef GE_SPOT_SURFACE_SHADOW_GLSL
#define GE_SPOT_SURFACE_SHADOW_GLSL
#include "shadow_filter.glsl"

float geSpotSurfaceShadow(sampler2DArray depthMap, mat4 projectionView, int layer,
    vec3 worldPosition, vec3 lightPosition, vec3 geometricNormal,
    vec3 worldPositionError, vec2 biasTexels)
{
    ivec3 size = textureSize(depthMap, 0);
    if (any(lessThanEqual(size, ivec3(0))) || layer < 0 || layer >= size.z) return 1.0;
    vec4 clip = projectionView * vec4(worldPosition, 1.0);
    if (!(clip.w > 0.0) || any(isnan(clip)) || any(isinf(clip))) return 1.0;
    vec3 receiver = clip.xyz / clip.w * 0.5 + 0.5;
    // A receiver outside the cone's map must not reuse a border texel.
    if (any(lessThan(receiver, vec3(0.0))) || any(greaterThan(receiver, vec3(1.0)))) return 1.0;

    vec3 rowX = vec3(projectionView[0][0], projectionView[1][0], projectionView[2][0]);
    vec3 rowY = vec3(projectionView[0][1], projectionView[1][1], projectionView[2][1]);
    vec3 rowZ = vec3(projectionView[0][2], projectionView[1][2], projectionView[2][2]);
    vec3 rowW = vec3(projectionView[0][3], projectionView[1][3], projectionView[2][3]);
    float worldTexel = max(2.0 * clip.w / (length(rowX) * float(size.x)),
                           2.0 * clip.w / (length(rowY) * float(size.y)));
    vec3 towardLight = lightPosition - worldPosition;
    float distanceSquared = dot(towardLight, towardLight);
    if (!(distanceSquared > 1e-12) || isnan(worldTexel) || isinf(worldTexel)) return 1.0;
    towardLight *= inversesqrt(distanceSquared);
    float normalSquared = dot(geometricNormal, geometricNormal);
    vec3 normal = normalSquared > 1e-10 ? geometricNormal * inversesqrt(normalSquared) : towardLight;
    if (dot(normal, towardLight) < 0.0) normal = -normal;
    float cosine = clamp(dot(normal, towardLight), 0.0, 1.0);
    float slope = sqrt(max(0.0, 1.0 - cosine * cosine));
    vec2 amount = clamp(biasTexels, vec2(0.0), vec2(8.0));
    vec3 offset = worldTexel * (towardLight * amount.x + normal * amount.y * max(slope, 0.70710678));
    // Apply an actual homogeneous displacement. Orthographic mat3 offsets
    // are incorrect here because both clip.z AND clip.w change.
    clip += projectionView * vec4(offset, 0.0);
    if (!(clip.w > 0.0) || any(isnan(clip)) || any(isinf(clip))) return 1.0;
    receiver = clip.xyz / clip.w * 0.5 + 0.5;
    if (receiver.z < 0.0 || receiver.z > 1.0) return 1.0;
    vec3 depthGradient = 0.5 * (rowZ - rowW * (clip.z / clip.w)) / clip.w;
    float positionMargin = dot(abs(depthGradient), max(worldPositionError, vec3(0.0)));
    float rounding = 2.0 * 1.1920928955078125e-7 * max(abs(receiver.z), 0.25);
    float bias = max(0.005 * length(depthGradient) + positionMargin, rounding);
    return geShadowPCF(depthMap, receiver, layer, bias);
}

#endif
