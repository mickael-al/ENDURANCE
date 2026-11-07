#version 460 core

in VS_OUT 
{
    vec2 uv;
    vec4 color;
} fs_in;

layout(std430, binding = 8) buffer depthBuffer
{
    uint data[];
}depth;

layout(binding = 10) uniform sampler2D u_texture;

out vec4 o_FragColor;

void main()
{

    vec4 col = texture(u_texture, fs_in.uv) * fs_in.color;

    // Dense cosmetic trails use low alpha; retain their soft edges and fade-out.
    if ((fs_in.uv.x > 1.0 || fs_in.uv.y > 1.0 || fs_in.uv.x < 0.0 || fs_in.uv.y < 0.0) || col.a <= 0.0001) { discard; }

    o_FragColor = col;
}
