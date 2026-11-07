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
    uvec4 detail; // floatBits(polar activity), volume IDs 0..5, IDs 6..7, reserved
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

float fieldSurfaceWeight(uvec4 ids,vec4 ratios,float marine,uint biome)
{
    float land=dot(vec4(equal(ids,uvec4(biome))),ratios);
    return land*(1.0-marine)+(biome==FIELD_MARINE_BIOME ? marine : 0.0);
}

// Each biome keeps its own editable recipe. Zero local weights skip its call;
// repeated matrix IDs are combined before the ordered accumulation.
vec4 fieldSurfaceRecipes(vec2 p,uvec4 ids,vec4 ratios,float marine)
{
    precise vec4 result=vec4(0.0);
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,0u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefPolarIce(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,1u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefFrozenTundra(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,2u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefTundra(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,3u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefTaiga(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,4u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefBorealWetland(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,5u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefRockyTundra(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,6u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefColdSteppe(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,7u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefBorealForest(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,8u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefColdForest(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,9u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefColdMarsh(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,10u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefDryPlain(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,11u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefPrairie(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,12u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefTemperateForest(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,13u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefHumidTemperateForest(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,14u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefTemperateMarsh(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,15u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefRockDesert(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,16u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefDrySavanna(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,17u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefSavanna(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,18u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefDryJungle(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,19u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefTropicalJungle(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,20u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefVolcanicDesert(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,21u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefRedDesert(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,22u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefAridPlateau(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,23u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefBurningJungle(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,24u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefTropicalMangrove(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    {
        float weight=fieldSurfaceWeight(ids,ratios,marine,25u);
        if(weight>0.0)
        {
            precise vec4 value=clamp(fieldReliefMarine(p),vec4(-640,0,-160,0),vec4(640,320,160,1));
            result+=value*weight;
        }
    }
    return clamp(result,vec4(-640.0,0.0,-160.0,0.0),vec4(640.0,320.0,160.0,1.0));
}

// Kept as a useful inspection helper; the authoritative IDs are CPU data.
vec4 fieldBlendRelief(vec2 p,vec2 climate)
{
    vec2 grid=clamp(climate,0.0,1.0)*4.0;
    uvec2 cell=min(uvec2(floor(grid)),uvec2(3u));
    vec2 f=fieldFade(grid-vec2(cell));
    uvec4 ids=fieldUnpackSurface(fieldBiomeCells[cell.x+4u*cell.y].x);
    return fieldSurfaceRecipes(p,ids,fieldQuadWeights(f),0.0);
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
    if(uBiomeOverride>=0 && uBiomeOverride<int(FIELD_BIOME_COUNT))
    {
        uint biome=uint(uBiomeOverride);
        volumeIDs=fieldRepeatedVolumeID(biome);
        if(uDetailScale>0.0)
            relief=fieldSurfaceRecipes(p,uvec4(biome),vec4(1,0,0,0),0.0);
        if(biome<FIELD_MARINE_BIOME)
            climate=vec2(biome%5u,biome/5u)*0.25;
    }
    else if(uDetailScale>0.0)
    {
        float marine=cell<16u ? 1.0-raw.blend.z : 0.0;
        relief=fieldSurfaceRecipes(p,fieldUnpackSurface(mapping.x),fieldQuadWeights(raw.blend.xy),marine);
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
    column.detail=uvec4(floatBitsToUint(polarActivity),volumeIDs,0u);
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
    precise float value=dot(vec4(equal(weights.idsLow,uvec4(biome))),weights.low);
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
    precise float density=base;
    {
        float weight=fieldVolumeWeight(weights,0u);
        if(weight>0.0)
        {
            precise float value=fieldVolumePolarIce(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,1u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeFrozenTundra(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,2u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTundra(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,3u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTaiga(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,4u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeBorealBog(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,5u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeRockyTundra(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,6u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeColdSteppe(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,7u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeBorealForest(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,8u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeColdForest(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,9u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeColdMarsh(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,10u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeDryPlain(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,11u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeMeadow(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,12u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTemperateForest(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,13u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTemperateRainforest(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,14u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTemperateMarsh(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,15u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeRockDesert(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,16u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeDrySavanna(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,17u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeSavanna(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,18u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeDryJungle(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,19u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTropicalJungle(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,20u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeVolcanicDesert(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,21u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeRedDesert(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,22u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeAridPlateau(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,23u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeBurningJungle(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,24u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeTropicalMangrove(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    {
        float weight=fieldVolumeWeight(weights,25u);
        if(weight>0.0)
        {
            precise float value=fieldVolumeMarine(p,column,noise);
            density+=(value-base)*weight;
        }
    }
    return density;
}

float fieldDensityAt(vec3 p,FieldColumn column)
{
    return fieldDensityAtPrepared(p,column,fieldColumnClimateRatio(column));
}

#endif
