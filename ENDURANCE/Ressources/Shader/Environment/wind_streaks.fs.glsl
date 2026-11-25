#version 450 core
#include "../../../../Asset/Shader/Atmosphere/sample.glsl"

in VS_OUT
{
    vec2 uv;
    vec4 color;
    vec3 worldPosition;
    float viewDepth;
    float nearFade;
} fs_in;
layout(binding=0) uniform sampler2D uEnvironmentOpaqueDepth;
uniform vec2 uEnvironmentInvResolution;
out vec4 o_FragColor;

void main()
{
    float edge=max(fwidth(fs_in.uv.y),.015);
    float crossSection=exp(-fs_in.uv.y*fs_in.uv.y*3.0)
        *(1.0-smoothstep(1.0-edge,1.0,abs(fs_in.uv.y)));
    float ends=smoothstep(0.0,.15,fs_in.uv.x)*(1.0-smoothstep(.75,1.0,fs_in.uv.x));
    float alpha=fs_in.color.a*crossSection*ends*fs_in.nearFade;
    if(alpha<.0001) discard;
    // Sample the separate compact opaque view-depth, never the attached depth
    // image. The usual hardware depth test handles opaque occlusion as well.
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy),ivec2(0),textureSize(uEnvironmentOpaqueDepth,0)-1);
    vec2 opaque=texelFetch(uEnvironmentOpaqueDepth,pixel,0).rg;
    alpha*=opaque.y==-2.0?1.0:clamp((-opaque.x-fs_in.viewDepth)/.65,0.0,1.0);
    if(alpha<.0001) discard;
    float daylight=uAtmosphereActive
        ? mix(.10,1.0,smoothstep(-.12,.22,geAtmSunDirection.y)*step(.5,geAtmSunDirection.w)) : 1.0;
    vec3 color=fs_in.color.rgb*mix(vec3(.16,.21,.30),vec3(1.0),daylight);
    color=geAtmComposite(color,fs_in.worldPosition,gl_FragCoord.xy*uEnvironmentInvResolution);
    o_FragColor=vec4(color,alpha);
}
