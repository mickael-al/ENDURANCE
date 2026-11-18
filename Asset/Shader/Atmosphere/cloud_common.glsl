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

bool geAtmHasClouds()
{
    return geAtmControl.z!=0 && geAtmCloudLayer.z>0.0 && geAtmCloudLayer.w>0.0;
}

float geAtmCloudMaximumRayDistance()
{
    return min(120000.0,max(geAtmCloudShape.x,1.0)*16.0);
}

// A bounded world-space slab intersection. The bound is measured FROM THE
// RAY ORIGIN, so a grazing ray cannot jump to an arbitrarily distant layer.
// Crossing the horizon continuously reduces the intersected path to zero.
vec2 geAtmCloudLayerPath(vec3 origin,vec3 direction,float maximum)
{
    if(!geAtmHasClouds()) return vec2(0.0);
    maximum=max(maximum,0.0);
    if(abs(direction.y)<1e-7)
        return origin.y>geAtmCloudLayer.x && origin.y<geAtmCloudLayer.x+geAtmCloudLayer.y
            ? vec2(0.0,maximum) : vec2(0.0);
    float a=(geAtmCloudLayer.x-origin.y)/direction.y;
    float b=(geAtmCloudLayer.x+geAtmCloudLayer.y-origin.y)/direction.y;
    float begin=clamp(min(a,b),0.0,maximum),end=clamp(max(a,b),0.0,maximum);
    return vec2(begin,max(begin,end));
}

// A regional 2D weather field is sampled from a fixed slice of the existing
// seeded noise atlas. Reusing its mip chain avoids another texture/dispatch.
// Its XZ scale is independent of the smaller 3D cloud shapes. Values are
// coverage and cloud type; both advect with the SAME domain as the clouds.
vec2 geAtmCloudWeatherAt(vec3 worldPosition,float footprint)
{
    float globalCoverage=clamp(geAtmCloudLayer.z,0.0,1.0);
    if(geAtmCloudWeather.y<=0.0 && geAtmCloudWeather.w<=0.0)
        return vec2(globalCoverage,0.5);
    float scale=max(geAtmCloudWeather.x,100.0);
    vec2 uv=(worldPosition.xz-geAtmCloudOffset.xy)/scale;
    float texels=max(footprint,0.0)*float(textureSize(geAtmCloudNoise,0).x)/scale;
    float lod=clamp(log2(max(texels,1.0)),0.0,float(max(textureQueryLevels(geAtmCloudNoise)-1,0)));
    vec4 weather=textureLod(geAtmCloudNoise,vec3(uv.x,0.371,uv.y),lod);
    float regional=clamp((weather.a-0.5)*max(geAtmCloudWeather.z,0.01)+0.5,0.0,1.0);
    // The global endpoints remain exact: coverage 0 clears every region and
    // coverage 1 fills every region, independent of the regional contrast.
    float variation=4.0*globalCoverage*(1.0-globalCoverage);
    float coverage=clamp(globalCoverage+(2.0*regional-1.0)
        *clamp(geAtmCloudWeather.y,0.0,1.0)*variation,0.0,1.0);
    return vec2(coverage,clamp(weather.g,0.0,1.0));
}

float geAtmCloudHeightProfile(float height,float type)
{
    vec3 weights=max(geAtmCloudProfiles.xyz,vec3(0.0));
    if(dot(weights,vec3(1.0))<1e-5) weights=vec3(0.0,1.0,0.0);
    weights/=dot(weights,vec3(1.0));
    vec3 distance=vec3(type)-vec3(0.15,0.5,0.85);
    vec3 regional=weights*exp(-18.0*distance*distance);
    regional/=max(dot(regional,vec3(1.0)),1e-6);
    weights=mix(weights,regional,clamp(geAtmCloudWeather.w,0.0,1.0));
    vec3 profile=vec3(
        smoothstep(0.0,0.04,height)*(1.0-smoothstep(0.18,0.32,height)),
        smoothstep(0.0,0.12,height)*(1.0-smoothstep(0.55,0.90,height)),
        smoothstep(0.0,0.12,height)*(1.0-smoothstep(0.88,1.0,height)));
    return dot(weights,profile);
}

float geAtmCloudDensityAtFootprint(vec3 worldPosition,float footprint)
{
    if(!geAtmHasClouds()) return 0.0;
    float height=(worldPosition.y-geAtmCloudLayer.x)/max(geAtmCloudLayer.y,1.0);
    if(height<=0.0 || height>=1.0) return 0.0;
    vec2 weather=geAtmCloudWeatherAt(worldPosition,footprint);
    if(weather.x<=0.0) return 0.0;
    // The CPU integrates velocity changes into a continuous displacement.
    // Weather, shape and all shadow rays sample the same advected domain.
    vec3 offset=vec3(geAtmCloudOffset.x,0.0,geAtmCloudOffset.y);
    vec3 p=(worldPosition-offset)/max(geAtmCloudShape.x,1.0);
    float texels=max(footprint,0.0)*float(textureSize(geAtmCloudNoise,0).x)/max(geAtmCloudShape.x,1.0);
    float lastLevel=float(max(textureQueryLevels(geAtmCloudNoise)-1,0));
    float lod=clamp(log2(max(texels,1.0)),0.0,lastLevel);
    float detailScale=max(geAtmCloudShape.y,1.0);
    vec4 shape=textureLod(geAtmCloudNoise,p,lod);
    float detail=textureLod(geAtmCloudNoise,p*detailScale+vec3(0.137,0.291,0.073),
        clamp(log2(max(texels*detailScale,1.0)),0.0,lastLevel)).b;
    float coverage=weather.x;
    float body=clamp((shape.r-(1.0-coverage))/max(coverage,0.001),0.0,1.0);
    body=clamp(body-(1.0-detail)*clamp(geAtmCloudShape.z,0.0,0.95)*(1.0-body),0.0,1.0);
    float profile=geAtmCloudHeightProfile(height,weather.y);
    return body*profile*max(geAtmCloudLayer.w,0.0);
}

float geAtmCloudDensity(vec3 worldPosition)
{
    return geAtmCloudDensityAtFootprint(worldPosition,0.0);
}

float geAtmCloudLightRay(vec3 position,vec3 sourceDirection,int samples)
{
    if(!geAtmHasClouds()) return 1.0;
    vec2 path=geAtmCloudLayerPath(position,sourceDirection,geAtmCloudMaximumRayDistance());
    float distance=path.y-path.x;
    if(distance<=0.0) return 1.0;
    samples=clamp(samples,2,64);
    float sum=0.0,stepLength=distance/float(samples);
    for(int i=0;i<samples;++i)
        sum+=geAtmCloudDensityAtFootprint(position+sourceDirection*(path.x+(float(i)+0.5)*stepLength),stepLength*0.25)*stepLength;
    return exp(-sum);
}

float geAtmCloudShadowHeight(vec3 sourceDirection)
{
    // Near-horizontal rays sample the middle of the layer; otherwise the map
    // integrates from its entry boundary. No binary sign/horizon threshold.
    float side=sourceDirection.y/sqrt(sourceDirection.y*sourceDirection.y+0.002*0.002);
    float boundary=0.5-0.5*side;
    float height=mix(0.5,boundary,smoothstep(0.002,0.02,abs(sourceDirection.y)));
    return geAtmCloudLayer.x+mix(0.1,max(geAtmCloudLayer.y-0.1,0.1),height);
}

float geAtmCloudVisibility(vec3 position,vec3 sourceDirection,bool secondary)
{
    if(!geAtmHasClouds()) return 1.0;
    vec2 path=geAtmCloudLayerPath(position,sourceDirection,geAtmCloudMaximumRayDistance());
    if(path.y<=path.x) return 1.0;
    float y=sourceDirection.y;
    // Regularized projection remains finite at exactly horizontal light.
    float distance=(geAtmCloudShadowHeight(sourceDirection)-position.y)*y/(y*y+0.002*0.002);
    vec2 anchor=position.xz+sourceDirection.xz*distance;
    vec2 uv=(anchor-geAtmShadowOrigin.xy)/(2.0*max(geAtmCloudOffset.z,1.0))+0.5;
    if(any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return 1.0;
    vec2 values=textureLod(geAtmCloudShadow,uv,0.0).rg;
    float whole=clamp(secondary ? values.g : values.r,0.0001,1.0);
    float wholeDistance=min(geAtmCloudMaximumRayDistance(),geAtmCloudLayer.y/max(abs(y),1e-7));
    float remaining=clamp((path.y-path.x)/max(wholeDistance,1.0),0.0,1.0);
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    float mapWeight=smoothstep(0.0,0.06,edge);
    // Outside a finite shadow domain the approximation fades to clear. This
    // lookup intentionally needs no noise sampler in the material pipeline.
    return mix(1.0,pow(whole,remaining),mapWeight);
}
#endif
