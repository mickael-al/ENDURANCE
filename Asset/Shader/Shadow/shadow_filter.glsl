#ifndef GE_SHADOW_FILTER_GLSL
#define GE_SHADOW_FILTER_GLSL

// Raw-depth samplers stay NEAREST for all consumers. Compare four texels
// first, then interpolate visibility: interpolating depths invents occluders.
// This works for orthographic and perspective maps after homogeneous division.
float geShadowPCF(sampler2DArray depthMap,vec3 receiver,int layer,float bias)
{
    ivec3 size=textureSize(depthMap,0);
    if(any(lessThanEqual(size,ivec3(0))) || layer<0 || layer>=size.z ||
        any(isnan(receiver)) || any(isinf(receiver)) || receiver.z<0.0 || receiver.z>1.0)
        return 1.0;
    vec2 samplePosition=receiver.xy*vec2(size.xy)-0.5;
    // Avoid float-to-integer overflow on invalid/out-of-coverage coordinates.
    if(any(lessThan(samplePosition,vec2(-1.0))) ||
        any(greaterThan(samplePosition,vec2(size.xy)))) return 1.0;
    ivec2 center=ivec2(floor(samplePosition));
    vec2 fraction=fract(samplePosition);
    float visibility=0.0;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x)
    {
        vec2 w=mix(vec2(1.0)-fraction,fraction,vec2(x,y));
        float weight=w.x*w.y;
        ivec2 pixel=center+ivec2(x,y);
        if(any(lessThan(pixel,ivec2(0))) || any(greaterThanEqual(pixel,size.xy)))
        { visibility+=weight; continue; }
        float storedDepth=texelFetch(depthMap,ivec3(pixel,layer),0).r;
        visibility+=weight*step(receiver.z-bias,storedDepth);
    }
    return visibility;
}

// CPU cascade fitting extends the following cascade backwards by this same
// fraction. Splits retain their original meaning; only the overlap is wider.
const float GE_SHADOW_CASCADE_BLEND_FRACTION=0.10;
float geShadowCascadeBlend(float viewZ,float previousSplit,float split)
{
    float end=-split;
    float width=max((previousSplit-split)*GE_SHADOW_CASCADE_BLEND_FRACTION,1e-5);
    return smoothstep(end-width,end,-viewZ);
}
#endif
