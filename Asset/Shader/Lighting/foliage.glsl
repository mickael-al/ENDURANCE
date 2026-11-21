// Thin, alpha-tested foliage in the existing deferred material layout.
// gColor.a >= 0: legacy opaque material; gColor.a < 0: transmission strength.
// The RGBA16F attachment already stores signed values. No new target/texture.
// This alpha is metadata AFTER the geometry pass has tested leaf coverage.
#ifndef GE_FOLIAGE_GLSL
#define GE_FOLIAGE_GLSL
#ifndef GE_FOLIAGE_LIGHTING
#define GE_FOLIAGE_LIGHTING 1
#endif

float geFoliageAmount(float materialAlpha)
{
#if GE_FOLIAGE_LIGHTING
    return clamp(-materialAlpha,0.0,1.0);
#else
    return 0.0;
#endif
}

vec2 geFoliageDiffuse(vec3 normal,vec3 viewDirection,vec3 lightDirection,float transmission)
{
    float nl=dot(normal,lightDirection);
    // Normalized wrapped diffuse preserves broad clump lighting at grazing
    // incidence. A modest reflection reduction leaves room for transmission.
    const float wrap=0.35;
    float wrapped=max(nl+wrap,0.0)/((1.0+wrap)*(1.0+wrap));
    float reflected=mix(max(nl,0.0),wrapped,transmission);
    float backView=max(dot(viewDirection,-lightDirection),0.0);
    float forwardLobe=backView*backView; forwardLobe*=forwardLobe;
    float transmitted=max(-nl,0.0)*(0.35+0.65*forwardLobe);
    // Both terms are multiplied by the actual light and its shadow visibility
    // by the caller. This is an artistic thin-leaf approximation, not emission.
    // Separate lobes: transmission must not use the reflection Fresnel.
    return vec2((1.0-0.35*transmission)*reflected,transmission*transmitted);
}
#endif
