// Frozen regression witness from 5c869c0be3ebb725e701f010c618105de237a897; do not update with the production helper.
// Directional surface shadows, independent of atmosphere/biome state.
// Receiver-plane correction follows the per-texel depth construction described
// in Microsoft's Cascaded Shadow Maps article. The caller supplies world-space
// position derivatives computed before divergent cascade/material branches.
// https://learn.microsoft.com/windows/win32/dxtecharts/cascaded-shadow-maps
#ifndef GE_DIRECTIONAL_SURFACE_SHADOW_GLSL
#define GE_DIRECTIONAL_SURFACE_SHADOW_GLSL

vec2 geDirectionalReceiverDepthGradient(mat4 projectionView,float homogeneousW,
    vec3 worldDx,vec3 worldDy)
{
    // Directional projections are affine. Keep the signs of all three axes:
    // the engine's orthographic projection deliberately reverses light-space Y.
    vec3 dx=(mat3(projectionView)*worldDx)*(0.5/homogeneousW);
    vec3 dy=(mat3(projectionView)*worldDy)*(0.5/homogeneousW);
    float scale=max(max(length(dx.xy),length(dy.xy)),1e-20);
    dx/=scale; dy/=scale;
    float determinant=dx.x*dy.y-dx.y*dy.x;
    float areaScale=sqrt(dot(dx.xy,dx.xy)*dot(dy.xy,dy.xy));
    // A collapsed/singular screen footprint cannot define z(u,v). Fall back
    // to a finite point comparison, never an arbitrarily large slope bias.
    if(!(abs(determinant)>max(1e-20,1e-5*areaScale))) return vec2(0.0);
    vec2 gradient=vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/determinant;
    if(any(isnan(gradient)) || any(isinf(gradient))) return vec2(0.0);
    return gradient;
}

float geDirectionalSurfaceResidualBias(mat4 projectionView,float referenceDepth,
    vec2 gradient,vec3 worldPositionError)
{
    vec3 rowX=vec3(projectionView[0][0],projectionView[1][0],projectionView[2][0]);
    vec3 rowY=vec3(projectionView[0][1],projectionView[1][1],projectionView[2][1]);
    vec3 rowZ=vec3(projectionView[0][2],projectionView[1][2],projectionView[2][2]);
    float depthPerMetre=0.5*length(rowZ);
    // Five millimetres cover residual geometric/rasterization error. A small
    // float-depth rounding margin is also needed when caster depth is extended.
    // A caller-supplied representation error affects both the receiver depth
    // and its UV on the receiver plane. Project that component-wise WORLD-space
    // error box through the signed plane equation. It is unrelated to texel
    // size/PCF radius and stays small for accurately represented geometry.
    float positionMargin=0.5*dot(max(worldPositionError,vec3(0.0)),
        abs(rowZ-gradient.x*rowX-gradient.y*rowY));
    float rounding=2.0*1.1920928955078125e-7*max(abs(referenceDepth),0.25);
    return max(0.005*depthPerMetre+positionMargin,rounding);
}

float geDirectionalSurfaceShadow(sampler2DArray depthMap,vec4 shadowCoord,int layer,
    mat4 projectionView,vec3 worldDx,vec3 worldDy,vec3 worldPositionError)
{
    if(!(shadowCoord.w>0.0) || any(isnan(shadowCoord)) || any(isinf(shadowCoord))) return 1.0;
    vec3 receiver=shadowCoord.xyz/shadowCoord.w;
    ivec3 size=textureSize(depthMap,0);
    if(any(lessThanEqual(size,ivec3(0))) || layer<0 || layer>=size.z ||
        any(lessThan(receiver,vec3(0.0))) || any(greaterThan(receiver,vec3(1.0)))) return 1.0;
    vec2 gradient=geDirectionalReceiverDepthGradient(projectionView,shadowCoord.w,worldDx,worldDy);
    ivec2 center=ivec2(floor(receiver.xy*vec2(size.xy)));
    float bias=geDirectionalSurfaceResidualBias(projectionView,receiver.z,gradient,worldPositionError);
    float visibility=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
    {
        ivec2 pixel=center+ivec2(x,y);
        if(any(lessThan(pixel,ivec2(0))) || any(greaterThanEqual(pixel,size.xy)))
        { visibility+=1.0; continue; }
        // NEAREST selects a texel center, not receiver.xy + integer offset.
        // Include the fractional receiver-to-center shift or a sub-texel grid
        // remains even after correcting the integer PCF offsets.
        vec2 tapCenter=(vec2(pixel)+0.5)/vec2(size.xy);
        float receiverDepth=receiver.z+dot(gradient,tapCenter-receiver.xy);
        if(receiverDepth<0.0 || receiverDepth>1.0) { visibility+=1.0; continue; }
        float storedDepth=texelFetch(depthMap,ivec3(pixel,layer),0).r;
        visibility+=step(receiverDepth-bias,storedDepth);
    }
    // Compare each unfiltered depth, then filter the visibility results.
    return visibility/9.0;
}
#endif
