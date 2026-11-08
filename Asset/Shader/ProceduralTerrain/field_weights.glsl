// Local interpolation is independent of the chunk, its LOD and its work list.
// No chunk-average cutoff is used: every positive local contribution survives.
#ifndef ENDURANCE_FIELD_WEIGHTS_GLSL
#define ENDURANCE_FIELD_WEIGHTS_GLSL
#include "field_common.glsl"

vec2 fieldLocalClimateRatio(FieldColumn column)
{
    vec2 grid=clamp(column.terrain.yz,0.0,1.0)*4.0;
    return fieldFade(grid-min(floor(grid),vec2(3.0)));
}

bool fieldForcedBiome()
{
    return uBiomeOverride>=0 && uBiomeOverride<int(FIELD_BIOME_COUNT);
}

float fieldSurfaceWeight(FieldColumn column,uint biome)
{
    if(fieldForcedBiome()) return uint(uBiomeOverride)==biome ? 1.0 : 0.0;
    uvec4 ids=fieldUnpackSurface(column.detail.x);
    vec4 quad=fieldQuadWeights(fieldLocalClimateRatio(column));
    uint cell=(column.detail.x>>20u)&63u;
    float marine=cell<16u ? 1.0-column.blend.x : 0.0;
    return dot(vec4(equal(ids,uvec4(biome))),quad*(1.0-marine))
        +(biome==FIELD_MARINE_BIOME ? marine : 0.0);
}

void fieldVolumeCorners(FieldColumn column,out uvec4 lowIDs,out uvec4 highIDs,
                        out vec4 low,out vec4 high)
{
    uvec2 encoded=column.detail.yz;
    lowIDs=uvec4(encoded.x,encoded.x>>5u,encoded.x>>10u,encoded.x>>15u)&31u;
    highIDs=uvec4(encoded.x>>20u,encoded.x>>25u,encoded.y,encoded.y>>5u)&31u;
    vec4 quad=fieldQuadWeights(fieldLocalClimateRatio(column));
    low=quad*(1.0-column.blend.x);
    high=quad*column.blend.x;
}

float fieldVolumeWeight(FieldColumn column,uint biome)
{
    if(fieldForcedBiome()) return uint(uBiomeOverride)==biome ? 1.0 : 0.0;
    uvec4 lowIDs,highIDs;
    vec4 low,high;
    fieldVolumeCorners(column,lowIDs,highIDs,low,high);
    return dot(vec4(equal(lowIDs,uvec4(biome))),low)
        +dot(vec4(equal(highIDs,uvec4(biome))),high);
}

uint fieldPositiveMask(uvec4 ids,vec4 weights)
{
    uvec4 bits=uvec4(greaterThan(weights,vec4(0.0)))<<ids;
    return bits.x|bits.y|bits.z|bits.w;
}

uint fieldSurfaceMask(FieldColumn column)
{
    if(fieldForcedBiome()) return 1u<<uint(uBiomeOverride);
    uint cell=(column.detail.x>>20u)&63u;
    float marine=cell<16u ? 1.0-column.blend.x : 0.0;
    return fieldPositiveMask(fieldUnpackSurface(column.detail.x),
        fieldQuadWeights(fieldLocalClimateRatio(column))*(1.0-marine))
        |(uint(marine>0.0)<<FIELD_MARINE_BIOME);
}

uint fieldVolumeMask(FieldColumn column)
{
    if(fieldForcedBiome()) return 1u<<uint(uBiomeOverride);
    uvec4 lowIDs,highIDs;
    vec4 low,high;
    fieldVolumeCorners(column,lowIDs,highIDs,low,high);
    return fieldPositiveMask(lowIDs,low)|fieldPositiveMask(highIDs,high);
}

// Every editable recipe must remain within these conservative world bounds.
// Offset and thickness have the same contract as the CPU's fieldHeightBounds.
vec2 fieldVolumeEnvelope(FieldColumn column)
{
    float offset=160.0*uDetailScale;
    float halfWidth=0.5*column.terrain.w;
    // Keep the helper's H + (global + recipeOffset) order. Reassociating this
    // as (H + global) + radius can round inward at large world coordinates.
    precise vec2 centers=vec2(column.terrain.x+(column.blend.y-offset),
                              column.terrain.x+(column.blend.y+offset));
    float padding=8.0*1.1920928955078125e-7*
        (abs(column.terrain.x)+abs(column.blend.y)+offset+halfWidth+1.0);
    return centers+vec2(-halfWidth-padding,halfWidth+padding);
}
#endif
