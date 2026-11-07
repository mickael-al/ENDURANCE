#version 450 core

in vec2 texCoord;
layout(location = 0) out vec4 o_prefilter;

layout(binding = 0) uniform sampler2D u_colorBuffer;
layout(binding = 1) uniform sampler2D u_depthBuffer;
uniform mat4 u_inverseProjection;
uniform float u_focusDistance;
uniform float u_focalLength;
uniform float u_fNumber;
uniform float u_sensorHeight;
uniform float u_renderHeight;
uniform float u_maxRadius;

// Thin-lens radius on the sensor, converted to full-resolution pixels.
// Units are metres; positive CoC is foreground, negative is background.
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
    ivec2 size = textureSize(u_colorBuffer, 0);
    ivec2 base = ivec2(gl_FragCoord.xy) * 2;
    vec3 colors[4];
    float radii[4];
    float nearRadius = 0.0;
    float farRadius = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        // Clamping the final 2x2 block also supports odd source dimensions.
        ivec2 pixel = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), size - 1);
        vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
        colors[i] = texelFetch(u_colorBuffer, pixel, 0).rgb;
        radii[i] = circleOfConfusion(uv, texelFetch(u_depthBuffer, pixel, 0).r);
        nearRadius = max(nearRadius, radii[i]);
        farRadius = min(farRadius, radii[i]);
    }

    // A visible foreground disk takes priority. Averaging RGB across a depth
    // silhouette would bake foreground colors into the background blur.
    bool foreground = nearRadius > 0.5;
    bool background = !foreground && farRadius < -0.5;
    vec3 color = vec3(0.0);
    float totalWeight = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        float weight = 1.0;
        if (foreground)
            weight = radii[i] > 0.0 ? max(radii[i], 0.001) : 0.0;
        else if (background)
            weight = radii[i] < 0.0 ? max(-radii[i], 0.001) : 0.0;
        color += colors[i] * weight;
        totalWeight += weight;
    }
    float radius = foreground ? nearRadius : (background ? farRadius : 0.0);
    o_prefilter = vec4(color / max(totalWeight, 1e-5), radius * 0.5);
}
