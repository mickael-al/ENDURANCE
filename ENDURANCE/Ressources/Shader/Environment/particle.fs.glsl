#version 450 core
#ifndef GE_BIOME_PARTICLE_SHAPE
#define GE_BIOME_PARTICLE_SHAPE 0
#endif
#include "../../../../Asset/Shader/Atmosphere/sample.glsl"

in VS_OUT
{
    vec2 uv;
    vec4 color;
    vec3 worldPosition;
    float viewDepth;
    flat float emission;
    flat float variation;
} fs_in;
layout(binding=0) uniform sampler2D uEnvironmentOpaqueDepth;
uniform vec2 uEnvironmentInvResolution;
out vec4 o_FragColor;

float coverage(vec2 p)
{
    float r2=dot(p,p);
#if GE_BIOME_PARTICLE_SHAPE == 0
    // Fine irregular ash, pollen and sand; smooth analytic edge, no texture.
    return (1.0-smoothstep(.18,1.0,r2))*(.72+.28*cos(p.x*7.0+p.y*5.0));
#elif GE_BIOME_PARTICLE_SHAPE == 1
    // Six crystalline arms remain broad enough to survive minification.
    float radial=sqrt(r2);
    float arms=pow(.5+.5*cos(atan(p.y,p.x)*6.0),5.0);
    return (1.0-smoothstep(.7,1.0,radial))*mix(.35,1.0,arms);
#elif GE_BIOME_PARTICLE_SHAPE == 2
    // Capsules stretched by the vertex shader: rain, drips, seeds and meteors.
    return (1.0-smoothstep(.12,1.0,abs(p.x)))*(1.0-smoothstep(.55,1.0,abs(p.y)));
#elif GE_BIOME_PARTICLE_SHAPE == 3
    // Pointed leaf/petal outline with a light midrib rather than round dots.
    float edge=abs(p.x)*1.25+abs(p.y)*abs(p.y);
    return (1.0-smoothstep(.7,1.0,edge))*(.72+.28*exp(-abs(p.x)*12.0));
#elif GE_BIOME_PARTICLE_SHAPE == 4
    // Emissive points retain a soft halo and a small bright core for bloom.
    return exp(-r2*5.0)*(1.0-smoothstep(.7,1.0,r2));
#elif GE_BIOME_PARTICLE_SHAPE == 5
    // Large vapor puffs: two broad lobes with no high-frequency FBM per pixel.
    vec2 q=p+vec2(.16*sin(p.y*4.0),.12*cos(p.x*3.0));
    float envelope=1.0-smoothstep(.1,1.0,dot(q,q));
    return envelope*envelope*.65;
#elif GE_BIOME_PARTICLE_SHAPE == 6
    // Faceted hail/ice diamonds with distinct internal triangular shading.
    return (1.0-smoothstep(.75,1.0,abs(p.x)+abs(p.y)))*(.55+.45*step(p.x,p.y));
#else
    // Two tiny wing lobes, preserving a distinct silhouette from pollen.
    float wings=exp(-dot((p-vec2(.36,0))*vec2(2.8,1.4),(p-vec2(.36,0))*vec2(2.8,1.4)))
               +exp(-dot((p+vec2(.36,0))*vec2(2.8,1.4),(p+vec2(.36,0))*vec2(2.8,1.4)));
    return clamp(wings,0.0,1.0)*(1.0-smoothstep(.65,1.0,r2));
#endif
}
void main()
{
    if(any(lessThan(fs_in.uv,vec2(0))) || any(greaterThan(fs_in.uv,vec2(1)))) discard;
    float alpha=fs_in.color.a*coverage(fs_in.uv*2.0-1.0);
    if(alpha<.001) discard;
    ivec2 pixel=clamp(ivec2(gl_FragCoord.xy),ivec2(0),textureSize(uEnvironmentOpaqueDepth,0)-1);
    vec2 opaque=texelFetch(uEnvironmentOpaqueDepth,pixel,0).rg;
    // Position R is negative view Z; G=-2 identifies the unobstructed sky.
    // This only softens the visible contact; depth testing remains authoritative.
    float soft=opaque.y==-2.0?1.0:clamp((-opaque.x-fs_in.viewDepth)/.65,0.0,1.0);
    alpha*=soft;
    if(alpha<.001) discard;
    float daylight=uAtmosphereActive
        ? mix(.10,1.0,smoothstep(-.12,.22,geAtmSunDirection.y)*step(.5,geAtmSunDirection.w)) : 1.0;
    vec3 illumination=mix(vec3(.16,.21,.30),vec3(1.0),daylight);
    // HDR emission contributes to the existing bloom; no point lights are added.
    vec3 color=fs_in.color.rgb*(illumination+vec3(fs_in.emission));
#if GE_BIOME_PARTICLE_SHAPE == 4
    color*=1.0+2.0*exp(-dot(fs_in.uv*2.0-1.0,fs_in.uv*2.0-1.0)*30.0);
#endif
    color=geAtmComposite(color,fs_in.worldPosition,gl_FragCoord.xy*uEnvironmentInvResolution);
    o_FragColor=vec4(color,alpha);
}
