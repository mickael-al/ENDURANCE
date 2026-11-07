#version 450 core

in vec2 texCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 0) uniform sampler2D u_color;

uniform vec3 u_tint_color;
uniform float u_strength;
uniform float u_saturation;
uniform float u_contrast;

void main()
{
    // This pass grades the display-referred image after bloom and tonemapping.
    vec4 source = texture(u_color, texCoord);
    vec3 color = mix(source.rgb, source.rgb * u_tint_color, u_strength);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, u_saturation);
    color = (color - vec3(0.5)) * u_contrast + vec3(0.5);
    FragColor = vec4(clamp(color, 0.0, 1.0), source.a);
}
