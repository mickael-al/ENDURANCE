#ifndef GE_ATMOSPHERE_CLOUD_COMMON_GLSL
#define GE_ATMOSPHERE_CLOUD_COMMON_GLSL
#include "atmosphere_common.glsl"
#ifndef GE_ATM_CLOUD_NOISE_BINDING
#define GE_ATM_CLOUD_NOISE_BINDING 3
#endif
#ifndef GE_ATM_CLOUD_SHADOW_BINDING
#define GE_ATM_CLOUD_SHADOW_BINDING 5
#endif
layout(binding=GE_ATM_CLOUD_NOISE_BINDING) uniform sampler3D geAtmCloudNoise;
layout(binding=GE_ATM_CLOUD_SHADOW_BINDING) uniform sampler2D geAtmCloudShadow;

float geAtmCloudDensityAtFootprint(vec3 worldPosition,float footprint)
{
    if(geAtmControl.z==0 || geAtmCloudLayer.z<=0.0 || geAtmCloudLayer.w<=0.0) return 0.0;
    float height=(worldPosition.y-geAtmCloudLayer.x)/max(geAtmCloudLayer.y,1.0);
    if(height<=0.0 || height>=1.0) return 0.0;
    vec3 wind=vec3(geAtmCloudWind.x,0.0,geAtmCloudWind.y)*geAtmCamera.w;
    vec3 p=(worldPosition-wind)/max(geAtmCloudShape.x,1.0);
    float texels=max(footprint,0.0)*float(textureSize(geAtmCloudNoise,0).x)/max(geAtmCloudShape.x,1.0);
    float lastLevel=float(max(textureQueryLevels(geAtmCloudNoise)-1,0));
    float lod=clamp(log2(max(texels,1.0)),0.0,lastLevel);
    float detailScale=max(geAtmCloudShape.y,1.0);
    vec4 shape=textureLod(geAtmCloudNoise,p,lod);
    float detail=textureLod(geAtmCloudNoise,p*detailScale+vec3(0.137,0.291,0.073),
        clamp(log2(max(texels*detailScale,1.0)),0.0,lastLevel)).b;
    float coverage=clamp(geAtmCloudLayer.z,0.0,1.0);
    float body=clamp((shape.r-(1.0-coverage))/max(coverage,0.001),0.0,1.0);
    body=clamp(body-(1.0-detail)*clamp(geAtmCloudShape.z,0.0,0.95)*(1.0-body),0.0,1.0);
    float profile=smoothstep(0.0,0.12,height)*(1.0-smoothstep(0.65,1.0,height));
    return body*profile*max(geAtmCloudLayer.w,0.0);
}

float geAtmCloudDensity(vec3 worldPosition)
{
    return geAtmCloudDensityAtFootprint(worldPosition,0.0);
}

float geAtmCloudLightRay(vec3 position,vec3 sourceDirection,int samples)
{
    if(geAtmControl.z==0) return 1.0;
    if(abs(sourceDirection.y)<0.001) return exp(-geAtmCloudDensity(position)*max(geAtmCloudShape.x,1.0)*4.0);
    float a=(geAtmCloudLayer.x-position.y)/sourceDirection.y;
    float b=(geAtmCloudLayer.x+geAtmCloudLayer.y-position.y)/sourceDirection.y;
    float begin=max(0.0,min(a,b)),end=max(a,b);
    if(end<=begin) return 1.0;
    float distance=min(end-begin,max(geAtmCloudShape.x,1.0)*16.0);
    samples=clamp(samples,2,64);
    float sum=0.0,stepLength=distance/float(samples);
    for(int i=0;i<samples;++i)
        sum+=geAtmCloudDensityAtFootprint(position+sourceDirection*(begin+(float(i)+0.5)*stepLength),stepLength*0.25)*stepLength;
    return exp(-sum);
}

float geAtmCloudVisibility(vec3 position,vec3 sourceDirection,bool secondary)
{
    if(geAtmControl.z==0 || geAtmCloudLayer.z<=0.0) return 1.0;
    float top=geAtmCloudLayer.x+geAtmCloudLayer.y;
    if(sourceDirection.y<=0.001) return position.y>top ? 1.0 : 0.0;
    if(position.y>=top) return 1.0;
    float distance=(geAtmCloudLayer.x-position.y)/sourceDirection.y;
    vec2 anchor=position.xz+sourceDirection.xz*distance;
    vec2 uv=(anchor-geAtmShadowOrigin.xy)/(2.0*max(geAtmCloudWind.z,1.0))+0.5;
    if(any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return 1.0;
    vec2 values=textureLod(geAtmCloudShadow,uv,0.0).rg;
    float whole=clamp(secondary ? values.g : values.r,0.0001,1.0);
    // The map stores the full layer. Interior points use remaining optical
    // depth; the cloud lighting pass uses an explicit light ray instead.
    float remaining=clamp((top-position.y)/max(geAtmCloudLayer.y,1.0),0.0,1.0);
    return pow(whole,remaining);
}

vec2 geAtmCloudLayerPath(vec3 origin,vec3 direction,float maximum)
{
    if(geAtmControl.z==0) return vec2(0.0);
    if(abs(direction.y)<1e-5)
        return origin.y>geAtmCloudLayer.x && origin.y<geAtmCloudLayer.x+geAtmCloudLayer.y
            ? vec2(0.0,maximum) : vec2(0.0);
    float a=(geAtmCloudLayer.x-origin.y)/direction.y;
    float b=(geAtmCloudLayer.x+geAtmCloudLayer.y-origin.y)/direction.y;
    return vec2(max(min(a,b),0.0),max(0.0,min(max(a,b),maximum)));
}
#endif
