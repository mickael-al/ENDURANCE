#version 450 core

in vec2 texCoord;
layout(location = 0) out vec4 o_color;

layout(binding = 0) uniform sampler2D u_colorBuffer;
layout(binding = 1) uniform sampler2D u_depthBuffer;
layout(binding = 2) uniform sampler2D u_farBlur;
layout(binding = 3) uniform sampler2D u_nearBlur;
uniform mat4 u_inverseProjection;
uniform float u_focusDistance;
uniform float u_focalLength;
uniform float u_fNumber;
uniform float u_sensorHeight;
uniform float u_renderHeight;
uniform float u_maxRadius;

float circleOfConfusion(vec2 uv, float depth)
{
    vec4 view = u_inverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    float objectDistance = abs(view.w) > 1e-7 ? max(-view.z / view.w, 1e-4) : 1e6;
    float focalLength = max(u_focalLength, 1e-5);
    float focusDistance = max(u_focusDistance, focalLength + 1e-3);
    float apertureRadius = focalLength / (2.0 * max(u_fNumber, 0.1));
    float radius = apertureRadius * focalLength * (focusDistance - objectDistance)
        / (objectDistance * (focusDistance - focalLength));
    radius *= max(u_renderHeight, 1.0) / max(u_sensorHeight, 1e-6);
    float limit = max(u_maxRadius, 0.0);
    return clamp(radius, -limit, limit);
}

void main()
{
    vec4 sharp = texture(u_colorBuffer, texCoord);
    if (u_maxRadius <= 0.0)
    {
        o_color = sharp;
        return;
    }

    float radius = circleOfConfusion(texCoord, texture(u_depthBuffer, texCoord).r);
    vec4 farBlur = texture(u_farBlur, texCoord);
    vec4 nearBlur = texture(u_nearBlur, texCoord);
    float farAmount = smoothstep(0.5, 1.5, -radius) * clamp(farBlur.a, 0.0, 1.0);
    vec3 color = mix(sharp.rgb, farBlur.rgb, farAmount);
    color = mix(color, nearBlur.rgb, clamp(nearBlur.a, 0.0, 1.0));
    // This single-depth approximation cannot recover background hidden behind
    // a foreground silhouette. The separate gathers suppress cross-depth halos.
    o_color = vec4(color, sharp.a);
}
