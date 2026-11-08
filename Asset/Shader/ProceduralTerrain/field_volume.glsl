// Density pass: consume prepared columns and evaluate only contributing 3D biomes.
#ifndef ENDURANCE_FIELD_VOLUME_GLSL
#define ENDURANCE_FIELD_VOLUME_GLSL

#include "field_common.glsl"
#include "field_noise_3d.glsl"
#include "biome_volume.glsl"

vec2 fieldColumnClimateRatio(FieldColumn column)
{
    vec2 grid=clamp(column.terrain.yz,0.0,1.0)*4.0;
    return fieldFade(grid-min(floor(grid),vec2(3.0)));
}

struct FieldBiomeWeights
{
    uvec4 idsLow;
    uvec4 idsHigh;
    vec4 low;
    vec4 high;
};

FieldBiomeWeights fieldVolumeWeights(FieldColumn column,vec2 climateRatio)
{
    FieldBiomeWeights result;
    uvec2 encoded=column.detail.yz;
    result.idsLow=uvec4(encoded.x,encoded.x>>5u,encoded.x>>10u,encoded.x>>15u)&31u;
    result.idsHigh=uvec4(encoded.x>>20u,encoded.x>>25u,encoded.y,encoded.y>>5u)&31u;
    vec4 quad=fieldQuadWeights(climateRatio);
    result.low=quad*(1.0-column.blend.x);
    result.high=quad*column.blend.x;
    if(uBiomeOverride>=0)
    {
        result.low=vec4(1,0,0,0);
        result.high=vec4(0.0);
    }
    return result;
}

float fieldVolumeWeight(FieldBiomeWeights weights,uint biome)
{
    // Repeated matrix IDs are aggregated before a recipe is evaluated.
    float value=dot(vec4(equal(weights.idsLow,uvec4(biome))),weights.low);
    value+=dot(vec4(equal(weights.idsHigh,uvec4(biome))),weights.high);
    return value;
}

float fieldDensityAtPrepared(vec3 p,FieldColumn column,vec2 climateRatio)
{
    if(uFieldMode==1u) return uBaseHeight-p.y;
    if(uFieldMode==2u) return uSphereRadius-length(p-uSphereCenter);
    float base=column.terrain.x-p.y;
    if(column.terrain.w<=0.0002 || uDetailScale<=0.0) return base;
    // Every recipe must honor the documented bounded volume band. This cheap
    // union test avoids decoding weights or running noise outside all bands.
    float distanceToGlobalCenter=abs(p.y-(column.terrain.x+column.blend.y));
    if(distanceToGlobalCenter>=0.5*column.terrain.w+160.0*uDetailScale) return base;
    FieldBiomeWeights weights=fieldVolumeWeights(column,climateRatio);
    FieldVolumeNoise noise;
    noise.ready=false;
    noise.values=vec3(0.0);
    float density=base;
    // The CPU built this candidate mask once per matrix cell; columns carry
    // it in their existing fourth metadata word. Local ratios still decide
    // whether each candidate contributes at this point.
    uint remaining=column.detail.w;
    while(remaining!=0u)
    {
        uint biome=uint(findLSB(remaining));
        remaining&=remaining-1u;
        float weight=fieldVolumeWeight(weights,biome);
        if(weight>0.0)
            density+=(fieldBiomeVolume(biome,p,column,noise)-base)*weight;
    }
    return density;
}

float fieldDensityAt(vec3 p,FieldColumn column)
{
    return fieldDensityAtPrepared(p,column,fieldColumnClimateRatio(column));
}

#endif
