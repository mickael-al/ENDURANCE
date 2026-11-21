#ifndef GE_ATMOSPHERE_SHADOWS_GLSL
#define GE_ATMOSPHERE_SHADOWS_GLSL
#include "atmosphere_common.glsl"
#include "../Shadow/shadow_filter.glsl"
struct GeAtmShadowMatrix { mat4 projview; vec3 position; float splitDepth; };
layout(std430,binding=5) readonly buffer GeAtmosphereShadows { GeAtmShadowMatrix geAtmShadowMatrices[]; };
layout(binding=4) uniform sampler2DArray geAtmTerrainShadow;

bool geAtmShadowReceiver(vec3 worldPosition,int layer,out vec3 receiver)
{
    vec4 clip=geAtmShadowMatrices[layer].projview*vec4(worldPosition,1.0);
    if(!(clip.w>0.0) || any(isnan(clip)) || any(isinf(clip))) return false;
    receiver=clip.xyz/clip.w*0.5+0.5;
    return !any(lessThan(receiver,vec3(0.0))) && !any(greaterThan(receiver,vec3(1.0)));
}

float geAtmShadowSample(vec3 receiver,int layer)
{
    // A medium has no receiver normal. Retain the world-space volume bias,
    // independent of the surface's geometric normal and caster depth span.
    float bias=geAtmDirectionalShadowBias(geAtmShadowMatrices[layer].projview,0.25);
    return geShadowPCF(geAtmTerrainShadow,receiver,layer,bias);
}

float geAtmTerrainVisibility(vec3 worldPosition,int firstLayer)
{
    int layerCount=min(geAtmShadow.z,min(geAtmShadowMatrices.length(),textureSize(geAtmTerrainShadow,0).z));
    if(!GE_ATM_TERRAIN_SHADOWS_ON || firstLayer<0 || firstLayer>=layerCount) return 1.0;
    float viewZ=(geAtmView*vec4(worldPosition,1.0)).z;
    int preferred=0;
    for(int i=0;i<3;++i)
        if(firstLayer+i<layerCount && viewZ<geAtmShadowMatrices[firstLayer+i].splitDepth) preferred=i+1;
    float visibility=0.0;
    float remaining=1.0;
    for(int cascade=preferred;cascade<4;++cascade)
    {
        int layer=firstLayer+cascade;
        if(layer>=layerCount) break;
        vec3 receiver;
        if(!geAtmShadowReceiver(worldPosition,layer,receiver)) continue;
        // Froxels can leave a cascade sideways. Fade its final texel to a
        // covering coarser cascade (or unoccluded light), rather than clamp
        // edge depths and then jump from a repeated occluder to full light.
        vec2 edgeTexels=min(receiver.xy,vec2(1.0)-receiver.xy)*vec2(textureSize(geAtmTerrainShadow,0).xy);
        float blend=1.0-smoothstep(0.0,1.0,min(edgeTexels.x,edgeTexels.y));
        if(cascade==preferred && cascade<3 && layer+1<layerCount)
        {
            float previousSplit=cascade>0?geAtmShadowMatrices[layer-1].splitDepth:0.0;
            blend=max(blend,geShadowCascadeBlend(viewZ,previousSplit,geAtmShadowMatrices[layer].splitDepth));
        }
        // Usually this stops after one map (four reads); a second is sampled
        // only in an overlap. Propagate edge coverage through any coarser maps
        // as well, including the rare case where several map edges coincide.
        if(blend<1.0) visibility+=remaining*(1.0-blend)*geAtmShadowSample(receiver,layer);
        remaining*=blend;
        if(remaining<=0.0) return visibility;
    }
    // Outside actual shadow coverage is explicitly lit, never edge-repeated.
    return visibility+remaining;
}
#endif
