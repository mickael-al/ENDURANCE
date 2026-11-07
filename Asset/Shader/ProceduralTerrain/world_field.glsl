// Deterministic scalar field: positive is solid, negative is air.
// CPU-owned biome matrices provide encoded cell corner IDs at binding 7.
#ifndef ENDURANCE_WORLD_FIELD_GLSL
#define ENDURANCE_WORLD_FIELD_GLSL

uniform uint uCells;
uniform ivec3 uOrigin;
uniform uint uLod;
uniform float uBaseSpacing;
uniform uint uSeed;
uniform uint uFieldMode;
uniform float uBaseHeight;
uniform float uHeightAmplitude;
uniform float uHeightFrequency;
uniform float uClimateFrequency;
uniform float uBandThickness;
uniform float uBandVariation;
uniform float uBandOffset;
uniform float uBandOffsetVariation;
uniform float uDetailScale;
uniform float uSeaLevel;
uniform float uSeaBlendWidth;
uniform int uBiomeOverride;
uniform vec3 uSphereCenter;
uniform float uSphereRadius;

// Three vec4-sized records, 48 bytes/std430 (previously 64).
// All floating-point field controls and interpolation ratios remain float32.
struct FieldColumn
{
    vec4 terrain; // surface height, humidity, temperature, total volume band
    vec4 blend;   // altitude ratio, global offset, relief offset, rock mask
    uvec4 detail; // floatBits(polar activity), volume IDs 0..5, IDs 6..7, candidate mask
};

#include "field_noise.glsl"
#include "biome_mapping.glsl"

vec2 fieldClimateAt(vec2 p)
{
    vec2 n=vec2(fieldNoise2(p*uClimateFrequency,0x243f6a88u),
                fieldNoise2(p*uClimateFrequency,0x85a308d3u));
    return smoothstep(vec2(-0.32),vec2(0.32),n);
}

// Compute local climate coordinates and matrix cell before evaluating relief.
// This intermediate stays within the column invocation, including its halo.
FieldColumn fieldClassifyColumn(vec2 p)
{
    FieldColumn column;
    column.terrain=vec4(uBaseHeight,0.5,0.5,0.0);
    column.blend=vec4(0.0);
    column.detail=uvec4(0u);
    if(uFieldMode!=0u) return column;
    float macroHeight=uBaseHeight;
    if(uHeightAmplitude>0.0)
        macroHeight+=fieldNoise2(p*uHeightFrequency,0x13198a2eu)*uHeightAmplitude;
    vec2 climate=fieldClimateAt(p);
    float coast=(macroHeight-uSeaLevel)/max(uSeaBlendWidth,0.001)+0.5;
    vec3 grid=vec3(clamp(climate,0.0,1.0)*4.0,clamp(coast,0.0,4.0));
    uvec3 cell=min(uvec3(floor(grid)),uvec3(3u));
    column.terrain=vec4(macroHeight,climate,0.0);
    column.blend=vec4(fieldFade(grid-vec3(cell)),0.0);
    column.detail.w=cell.x+4u*cell.y+16u*cell.z;
    return column;
}

vec3 fieldWorldPosition(ivec3 sampleIndex)
{
    ivec3 lattice=uOrigin+(sampleIndex-ivec3(1))*int(1u<<uLod);
    return vec3(lattice)*uBaseSpacing;
}

#include "biome_relief.glsl"
#include "biome_volume.glsl"

// IDs are validated on the CPU (0..25). OR merges repeated corner IDs;
// the mask is invocation-local and requires no atomics or temporary arrays.
uint fieldActiveBiomeMask(uvec4 ids,vec4 weights)
{
    uvec4 bits=uvec4(greaterThan(weights,vec4(0.0)))<<ids;
    return bits.x|bits.y|bits.z|bits.w;
}

// Visit only positive-weight biomes (four surface corners plus the sea).
// Lowest-bit traversal keeps accumulation ordered by biome ID.
vec4 fieldSurfaceRecipes(vec2 p,uvec4 ids,vec4 ratios,float marine)
{
    vec4 landWeights=ratios*(1.0-marine);
    uint remaining=fieldActiveBiomeMask(ids,landWeights)
        | (uint(marine>0.0)<<FIELD_MARINE_BIOME);
    vec4 result=vec4(0.0);
    while(remaining!=0u)
    {
        uint biome=uint(findLSB(remaining));
        remaining&=remaining-1u;
        float weight=dot(vec4(equal(ids,uvec4(biome))),landWeights)
            +(biome==FIELD_MARINE_BIOME ? marine : 0.0);
        result+=fieldBiomeRelief(biome,p)*weight;
    }
    // Preserve the conservative column bounds even after blend rounding.
    return clamp(result,vec4(-640.0,0.0,-160.0,0.0),vec4(640.0,320.0,160.0,1.0));
}

FieldColumn fieldColumnFromClimate(vec2 p,FieldColumn raw)
{
    FieldColumn column;
    column.terrain=raw.terrain;
    column.blend=vec4(0.0,uBandOffset,0.0,1.0);
    column.detail=uvec4(0u);
    if(uFieldMode!=0u) return column;
    uint cell=raw.detail.w;
    uvec4 mapping=fieldBiomeCells[cell];
    vec4 relief=vec4(0.0,0.0,0.0,1.0);
    vec2 climate=raw.terrain.yz;
    uvec2 volumeIDs=mapping.yz;
    uint volumeMask=mapping.w;
    bool overrideBiome=uBiomeOverride>=0 && uBiomeOverride<int(FIELD_BIOME_COUNT);
    if(overrideBiome)
    {
        uint biome=uint(uBiomeOverride);
        volumeIDs=fieldRepeatedVolumeID(biome);
        volumeMask=1u<<biome;
        if(biome<FIELD_MARINE_BIOME)
            climate=vec2(biome%5u,biome/5u)*0.25;
    }
    if(uDetailScale>0.0)
    {
        uvec4 surfaceIDs;
        vec4 surfaceRatios;
        float marine;
        if(overrideBiome)
        {
            surfaceIDs=uvec4(uint(uBiomeOverride));
            surfaceRatios=vec4(1,0,0,0);
            marine=0.0;
        }
        else
        {
            surfaceIDs=fieldUnpackSurface(mapping.x);
            surfaceRatios=fieldQuadWeights(raw.blend.xy);
            marine=cell<16u ? 1.0-raw.blend.z : 0.0;
        }
        // One call site avoids inlining all 26 recipes separately for the
        // forced-biome and matrix paths. Only their local blend inputs differ.
        relief=fieldSurfaceRecipes(p,surfaceIDs,surfaceRatios,marine);
    }
    float height=raw.terrain.x+relief.x*uDetailScale;
    float thickness=uBandThickness+relief.y*uDetailScale;
    if(uBandVariation>0.0)
        thickness+=uBandVariation*fieldUnsignedNoise(p*0.0001,0x03707344u);
    float globalOffset=uBandOffset;
    if(uBandOffsetVariation>0.0)
        globalOffset+=uBandOffsetVariation*fieldNoise2(p*0.0004,0xa4093822u);
    float polarActivity=0.0;
    if(uDetailScale>0.0 && (uBiomeOverride==0 ||
        (uBiomeOverride<0 && fieldMappedVolumeWeight(volumeIDs,raw.blend.xyz,0u)>0.0)))
        polarActivity=polarVolumeActivity(p);
    column.terrain=vec4(height,climate,max(0.0,thickness));
    column.blend=vec4(raw.blend.z,globalOffset,relief.z,relief.w);
    column.detail=uvec4(floatBitsToUint(polarActivity),volumeIDs,volumeMask);
    return column;
}

FieldColumn fieldColumnAt(vec2 p)
{
    return fieldColumnFromClimate(p,fieldClassifyColumn(p));
}

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
