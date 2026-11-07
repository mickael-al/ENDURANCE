#version 460 core

in VS_OUT
{
    vec2 uv;
    vec4 color;
} fs_in;

layout(std430, binding = 3) buffer UniformBufferDivers
{
    int maxLight;
    float u_time;
    float gamma;
    float ambiant;
    float fov;
    bool ortho;
} ubd;

layout(binding = 10) uniform sampler2D u_texture;

out vec4 o_FragColor;

void main()
{
    vec2 uv = fs_in.uv;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        discard;
    }

    // Particle UVs originate at the bottom-left while the PNG is loaded from
    // its top-left row. Keep the screen-space height before flipping the
    // texture coordinate so the procedural sway still grows toward the tip.
    float height = clamp(uv.y, 0.0, 1.0);
    uv.y = 1.0 - uv.y;
    float sway =
        sin(height * 13.0 + ubd.u_time * 7.2) +
        sin(height * 29.0 - ubd.u_time * 10.3) * 0.42;
    uv.x += sway * 0.018 * smoothstep(0.08, 0.92, height);
    if (uv.x < 0.0 || uv.x > 1.0)
    {
        discard;
    }

    vec4 sprite = texture(u_texture, uv);
    float alpha = sprite.a * clamp(fs_in.color.a, 0.0, 1.0);
    if (alpha <= 0.01)
    {
        discard;
    }

    float luminance = dot(sprite.rgb, vec3(0.299, 0.587, 0.114));
    float shimmer = 0.93 + 0.07 * sin(
        ubd.u_time * 12.0 + height * 18.0 + uv.x * 7.0);
    float hotCore = pow(
        clamp(1.0 - abs(uv.x - 0.5) * 2.0, 0.0, 1.0),
        3.0) * (1.0 - height * 0.38);
    vec3 color = fs_in.color.rgb *
        mix(0.72, 1.18, luminance) * shimmer;
    color += vec3(0.32, 0.07, 0.006) * hotCore * luminance;

    o_FragColor = vec4(color, alpha);
}
