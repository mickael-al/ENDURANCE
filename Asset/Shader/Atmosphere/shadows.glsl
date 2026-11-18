#ifndef GE_ATMOSPHERE_SHADOWS_GLSL
#define GE_ATMOSPHERE_SHADOWS_GLSL
#include "atmosphere_common.glsl"
struct GeAtmShadowMatrix { mat4 projview; vec3 position; float splitDepth; };
layout(std430,binding=5) readonly buffer GeAtmosphereShadows { GeAtmShadowMatrix geAtmShadowMatrices[]; };
layout(binding=4) uniform sampler2DArray geAtmTerrainShadow;

float geAtmTerrainVisibility(vec3 worldPosition,int firstLayer)
{
    if(geAtmControl.w==0 || firstLayer<0 || firstLayer>=geAtmShadow.z) return 1.0;
    float viewZ=(geAtmView*vec4(worldPosition,1.0)).z;
    int preferred=0;
    for(int i=0;i<3;++i)
        if(firstLayer+i<geAtmShadow.z && viewZ<geAtmShadowMatrices[firstLayer+i].splitDepth) preferred=i+1;
    for(int cascade=preferred;cascade<4;++cascade)
    {
        int layer=firstLayer+cascade;
        if(layer>=geAtmShadow.z) break;
        vec4 clip=geAtmShadowMatrices[layer].projview*vec4(worldPosition,1.0);
        if(clip.w<=0.0) continue;
        vec3 uvz=clip.xyz/clip.w*0.5+0.5;
        if(any(lessThan(uvz,vec3(0.0))) || any(greaterThan(uvz,vec3(1.0)))) continue;
        vec2 texel=1.0/vec2(textureSize(geAtmTerrainShadow,0).xy);
        float bias=geAtmDirectionalShadowBias(geAtmShadowMatrices[layer].projview,0.25);
        float visibility=0.0;
        for(int y=0;y<2;++y) for(int x=0;x<2;++x)
        {
            vec2 uv=uvz.xy+(vec2(x,y)-0.5)*texel;
            float depth=textureLod(geAtmTerrainShadow,vec3(uv,float(layer)),0.0).r;
            visibility+=step(uvz.z-bias,depth);
        }
        return visibility*0.25;
    }
    // Outside actual shadow coverage is explicitly lit, never edge-repeated.
    return 1.0;
}
#endif
