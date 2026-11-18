#version 430 core
#include "../../Asset/Shader/Shadow/directional_surface.glsl"
layout(binding=0) uniform sampler2DArray depthMap;
uniform mat4 projectionView;
uniform mat4 inverseProjectionView;
uniform vec3 plane;
uniform vec4 receiverDomain;
uniform int layer;
uniform int degenerateMode;
uniform float homogeneousW;
uniform vec3 worldPositionError;
in vec2 TexCoords;
layout(location=0) out vec4 visibility;
layout(location=1) out vec4 diagnostics;

// Negative control: the original surface PCF uses the same receiver depth for
// every nearest-filtered tap and the old world-texel/slope bias.
float oldBias(vec3 dx,vec3 dy)
{
    vec3 normal=normalize(cross(dx,dy));
    vec3 light=normalize(inverseProjectionView[2].xyz);
    float cosine=clamp(abs(dot(normal,light)),0.0,1.0);
    float slope=min(sqrt(max(1.0-cosine*cosine,0.0))/max(cosine,0.1),4.0);
    float width=2.0/length(vec3(projectionView[0][0],projectionView[1][0],projectionView[2][0]));
    float texel=width/float(textureSize(depthMap,0).x);
    float worldBias=0.025+texel*(0.15+0.35*slope);
    return 0.5*length(vec3(projectionView[0][2],projectionView[1][2],projectionView[2][2]))*worldBias;
}

float oldPcf(vec3 sc,float bias)
{
    if(any(lessThan(sc,vec3(0))) || any(greaterThan(sc,vec3(1)))) return 1.0;
    vec2 texel=1.0/vec2(textureSize(depthMap,0).xy);
    float result=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
    {
        vec2 uv=sc.xy+vec2(x,y)*texel;
        if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) result+=1.0;
        else result+=step(sc.z-bias,texture(depthMap,vec3(uv,layer)).r);
    }
    return result/9.0;
}

// A tempting partial fix corrects only integer PCF offsets. Keep it as a
// negative control for the easily missed fractional receiver-to-texel shift.
float integerOffsetsOnly(vec3 sc,vec2 gradient,float bias)
{
    vec2 texel=1.0/vec2(textureSize(depthMap,0).xy);
    float result=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
    {
        vec2 offset=vec2(x,y)*texel;
        result+=step(sc.z+dot(gradient,offset)-bias,texture(depthMap,vec3(sc.xy+offset,layer)).r);
    }
    return result/9.0;
}

void main()
{
    vec2 uv=receiverDomain.xy+TexCoords*receiverDomain.zw;
    // Work in a camera-relative frame for derivatives, as the deferred caller
    // does with its 32-bit view-position GBuffer. Translation only affects the
    // final world position sent through the actual shadow projection.
    // Construct the geometry BEFORE normalized depth quantization. Rebuilding
    // world positions from a rounded .5 + tinyDepthSlope would invent bumps
    // absent from the actual planar receiver and from the depth raster.
    vec3 relative=mat3(inverseProjectionView)*vec3(2.0*uv-1.0,2.0*dot(plane.xy,uv-0.5));
    vec3 world=relative+inverseProjectionView[3].xyz+
        inverseProjectionView[2].xyz*(2.0*plane.z-1.0);
    vec3 dx=dFdx(relative),dy=dFdy(relative);
    // All derivatives above are evaluated before the divergent test branches.
    vec4 projected=projectionView*vec4(world,1.0);
    vec4 sc=vec4(projected.xyz*0.5+0.5,1.0)*homogeneousW;
    vec3 receiver=projected.xyz*0.5+0.5;
    float previous=oldPcf(receiver,oldBias(dx,dy));
    if(degenerateMode==1) { dx=vec3(0);dy=vec3(0); }
    if(degenerateMode==2) dy=dx;
    vec2 gradient=geDirectionalReceiverDepthGradient(projectionView,max(homogeneousW,1e-10),dx,dy);
    float bias=geDirectionalSurfaceResidualBias(projectionView,receiver.z,gradient,worldPositionError);
    float corrected=geDirectionalSurfaceShadow(depthMap,sc,layer,projectionView,dx,dy,worldPositionError);
    visibility=vec4(previous,corrected,integerOffsetsOnly(receiver,gradient,bias),bias);
    diagnostics=vec4(receiver,abs(gradient.x-plane.x)+abs(gradient.y-plane.y));
}
