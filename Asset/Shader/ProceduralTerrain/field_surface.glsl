// Column pass: climate, local biome weights and 2D relief, evaluated once per XZ.
#ifndef ENDURANCE_FIELD_SURFACE_GLSL
#define ENDURANCE_FIELD_SURFACE_GLSL

#include "field_common.glsl"
#include "field_noise_2d.glsl"
#include "biome_relief.glsl"

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
    return clamp(result,vec4(-640.0,0.0,-160.0,0.0),vec4(640.0,576.0,160.0,1.0));
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
    // The biome owns the entire band: zero stays zero, with no global floor.
    float thickness=relief.y*uDetailScale;
    float globalOffset=uBandOffset;
    if(uBandOffsetVariation>0.0)
        globalOffset+=uBandOffsetVariation*fieldNoise2(p*0.0004,0xa4093822u);
    column.terrain=vec4(height,climate,max(0.0,thickness));
    column.blend=vec4(raw.blend.z,globalOffset,relief.z,relief.w);
    column.detail=uvec4(0u,volumeIDs,volumeMask);
    return column;
}

FieldColumn fieldColumnAt(vec2 p)
{
    return fieldColumnFromClimate(p,fieldClassifyColumn(p));
}

#endif
