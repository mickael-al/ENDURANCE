#ifndef ENDURANCE_GAME_FOG_GLSL
#define ENDURANCE_GAME_FOG_GLSL
#include "../../../../Asset/Shader/Noise/noise_3d.glsl"
layout(binding=6) uniform sampler2D uGameFogMedium;
layout(binding=7) uniform sampler2D uGameFogShape;
uniform vec4 uGameFogOriginSize;
uniform int uGameFogAtlasValid;
uniform uint uGameFogSeed;
// Integrated world-space wind displacement, in metres. The game advances this
// every rendered frame; climate atlas placement and height falloff stay fixed.
uniform vec3 uGameFogWindOffset;

// CPU blends at most two environment layers. Ground fog complements (rather
// than doubles) the biome fog; suspended weather is an additional medium.
// These uniforms affect volume integration only, never the sky/IBL/LUT caches.
uniform vec4 uEnvironmentMedium[2]; // sigma_s RGB, sigma_t, inverse metres
uniform vec4 uEnvironmentShape[2];  // base offset, height scale, noise amount/frequency
uniform vec4 uEnvironmentWind[2];   // integrated particle wind XZ, sea level, unused

vec4 geGameBaseFog(vec3 worldPosition,out float groundBase)
{
    // The climate atlas describes an authored altitude, not the terrain surface.
    // A missing atlas must not prevent weather from appearing around the player.
    groundBase=uEnvironmentWind[0].z;
    if(uGameFogAtlasValid==0 || uGameFogOriginSize.z<=0.0) return vec4(0.0);
    vec2 uv=(worldPosition.xz-uGameFogOriginSize.xy)/uGameFogOriginSize.z;
    if(any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return vec4(0.0);
    vec4 medium=max(textureLod(uGameFogMedium,uv,0.0),vec4(0.0));
    if(medium.a<=1e-10 && uEnvironmentMedium[0].a<=1e-10) return vec4(0.0);
    vec4 shape=textureLod(uGameFogShape,uv,0.0);
    groundBase=shape.x;
    if(medium.a<=1e-10) return vec4(0.0);
    float density=exp(-max(worldPosition.y-shape.x,0.0)/max(shape.y,1.0));
    if(shape.z>0.001)
    {
        float n=noiseValueFbm3D01((worldPosition-uGameFogWindOffset)*max(shape.w,0.000001),
                                  uGameFogSeed^0x3c6ef372u,3,2.0,0.5);
        density*=mix(1.0,smoothstep(0.25,0.72,n)*1.35,clamp(shape.z,0.0,1.0));
    }
    return medium*density;
}

vec4 geEnvironmentMedium(vec3 worldPosition,int layer,float baseHeight)
{
    vec4 medium=uEnvironmentMedium[layer];
    // This is uniform for the dispatch: an inactive effect samples no noise.
    if(medium.a<=1e-10) return vec4(0.0);
    vec4 shape=uEnvironmentShape[layer];
    float density=exp(-max(worldPosition.y-baseHeight-shape.x,0.0)/max(shape.y,1.0));
    if(shape.z>0.001)
    {
        vec3 p=worldPosition;
        p.xz-=uEnvironmentWind[layer].xy;
        // One broad value-noise octave, not another per-ray FBM. The stretched
        // vertical coordinate forms rolling sheets without screen-space bands.
        p.y*=0.65;
        float n=noiseValue3D01(p*max(shape.w,0.000001),uGameFogSeed^0xa511e9b3u);
        density*=mix(1.0,mix(0.2,1.4,smoothstep(0.2,0.8,n)),clamp(shape.z,0.0,1.0));
    }
    return medium*density;
}

// Returns physical coefficients (sigma_s RGB, sigma_t), in inverse metres.
// Combined before the engine's Beer-Lambert/light integration, so the terrain,
// trees and transparent objects receive the same depth-dependent atmosphere.
vec4 geGameFog(vec3 worldPosition)
{
    float groundBase;
    vec4 medium=geGameBaseFog(worldPosition,groundBase);
    vec4 ground=geEnvironmentMedium(worldPosition,0,groundBase);
    // Keep the denser ground layer; adding both would reproduce the unwanted
    // duplicated ground mist. Only its missing extinction/scattering is added.
    float extra=max(ground.a-medium.a,0.0);
    medium+=ground*(extra/max(ground.a,1e-10));
    return medium+geEnvironmentMedium(worldPosition,1,uEnvironmentWind[1].z);
}
#endif
