#ifndef ENDURANCE_GAME_FOG_GLSL
#define ENDURANCE_GAME_FOG_GLSL
#include "../../../../Asset/Shader/Noise/noise_3d.glsl"
layout(binding=6) uniform sampler2D uGameFogMedium;
layout(binding=7) uniform sampler2D uGameFogShape;
uniform vec4 uGameFogOriginSize;
uniform int uGameFogAtlasValid;
uniform uint uGameFogSeed;

// Returns physical coefficients (sigma_s RGB, sigma_t), in inverse metres.
// The camera's selected biome is never used to place the medium in the world.
vec4 geGameFog(vec3 worldPosition)
{
    if(uGameFogAtlasValid==0 || uGameFogOriginSize.z<=0.0) return vec4(0.0);
    vec2 uv=(worldPosition.xz-uGameFogOriginSize.xy)/uGameFogOriginSize.z;
    if(any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return vec4(0.0);
    vec4 medium=max(textureLod(uGameFogMedium,uv,0.0),vec4(0.0));
    if(medium.a<=1e-10) return vec4(0.0);
    vec4 shape=textureLod(uGameFogShape,uv,0.0);
    float density=exp(-max(worldPosition.y-shape.x,0.0)/max(shape.y,1.0));
    if(shape.z>0.001)
    {
        float n=noiseValueFbm3D01(worldPosition*max(shape.w,0.000001),
                                  uGameFogSeed^0x3c6ef372u,3,2.0,0.5);
        density*=mix(1.0,smoothstep(0.25,0.72,n)*1.35,clamp(shape.z,0.0,1.0));
    }
    return medium*density;
}
#endif
