#version 450

// Shared fullscreen VAO: identical layout across lighting variants and post passes.
layout(location = 0) in vec3 a_Position;
in vec3 a_Normal;
in vec3 a_Tangents;
in vec3 a_Color;
layout(location = 1) in vec2 a_TexCoords;

out vec2 texCoord;

void main(void)
{
	texCoord = a_TexCoords;
	gl_Position = vec4(a_Position, 1.0);
}
