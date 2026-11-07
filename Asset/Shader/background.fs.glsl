#version 450

layout(location = 0) in vec3 WorldPos;
layout(location = 1) in vec3 ViewPos;

uniform samplerCube environmentMap;
uniform float skyboxSaturation;

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gColor;
layout(location = 3) out vec3 gOther;

void main()
{		
    vec3 envColor = texture(environmentMap, WorldPos).rgb;
    float luminance = dot(envColor, vec3(0.2126, 0.7152, 0.0722));
    envColor = mix(vec3(luminance), envColor, skyboxSaturation);

    gColor = vec4(pow(max(envColor, vec3(0.0)), vec3(1.0/2.2)),1.0);
    //gColor = vec4(envColor,1.0);
    gPosition = vec4(ViewPos,1.0f);    
}
