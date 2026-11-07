#version 450 core

in vec2 texCoord;
layout(location = 0) out vec4 o_farBlur;
layout(location = 1) out vec4 o_nearBlur;

layout(binding = 0) uniform sampler2D u_prefilter;
uniform float u_maxRadius; // Half-resolution pixels.

const int SAMPLE_COUNT = 48;
const float GOLDEN_ANGLE = 2.39996323;

bool insideImage(vec2 uv, vec2 halfTexel)
{
    return all(greaterThanEqual(uv, halfTexel))
        && all(lessThanEqual(uv, vec2(1.0) - halfTexel));
}

// Pixel-wide transition softens the boundary of the sampled aperture disk.
float diskCoverage(float radius, float distance)
{
    return 1.0 - smoothstep(max(radius - 0.5, 0.0), radius + 0.5, distance);
}

void main()
{
    ivec2 size = textureSize(u_prefilter, 0);
    vec2 texel = 1.0 / vec2(size);
    vec2 halfTexel = texel * 0.5;
    vec4 center = texelFetch(u_prefilter, ivec2(gl_FragCoord.xy), 0);
    float farRadius = max(-center.a, 0.0);
    float searchRadius = max(u_maxRadius, 0.0);
    vec3 farColor = vec3(0.0);
    float farWeight = 0.0;
    vec3 nearColor = vec3(0.0);
    float nearWeight = 0.0;
    float nearSearchWeight = 0.0;

    // Deterministic bounded disk taps avoid temporal noise. Both gathers share
    // the same pattern but use different radii; foreground must search beyond
    // its silhouette, including pixels whose own CoC is zero or negative.
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float radial = sqrt((float(i) + 0.5) / float(SAMPLE_COUNT));
        float angle = float(i) * GOLDEN_ANGLE;
        vec2 disk = vec2(cos(angle), sin(angle)) * radial;

        if (farRadius > 0.0)
        {
            vec2 uv = texCoord + disk * farRadius * texel;
            if (insideImage(uv, halfTexel))
            {
                // CoC and RGB are fetched from the same discrete source pixel;
                // bilinear interpolation could mix colors across a silhouette
                // before the CoC rejection gets a chance to protect it.
                ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
                vec4 sampleValue = texelFetch(u_prefilter, pixel, 0);
                float distance = length((vec2(pixel) + 0.5) - texCoord * vec2(size));
                if (sampleValue.a < -0.25)
                {
                    float weight = diskCoverage(min(farRadius, max(-sampleValue.a, 0.0)),
                                                distance);
                    farColor += sampleValue.rgb * weight;
                    farWeight += weight;
                }
            }
        }

        if (searchRadius > 0.0)
        {
            vec2 uv = texCoord + disk * searchRadius * texel;
            if (insideImage(uv, halfTexel))
            {
                nearSearchWeight += 1.0;
                ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
                vec4 sampleValue = texelFetch(u_prefilter, pixel, 0);
                float distance = length((vec2(pixel) + 0.5) - texCoord * vec2(size));
                if (sampleValue.a > 0.25)
                {
                    // Only a source's own foreground disk can cover this pixel.
                    // Inverse disk area keeps large bokeh from dominating small
                    // ones and estimates their opacity from sample density.
                    float radius = sampleValue.a;
                    float weight = diskCoverage(radius, distance)
                        / max(radius * radius, 0.25);
                    nearColor += sampleValue.rgb * weight;
                    nearWeight += weight;
                }
            }
        }
    }

    o_farBlur = vec4(farWeight > 1e-5 ? farColor / farWeight : center.rgb,
                     farWeight > 1e-5 ? 1.0 : 0.0);
    float nearCoverage = clamp(nearWeight * searchRadius * searchRadius
                              / max(nearSearchWeight, 1.0), 0.0, 1.0);
    o_nearBlur = vec4(nearWeight > 1e-5 ? nearColor / nearWeight : vec3(0.0), nearCoverage);
}
