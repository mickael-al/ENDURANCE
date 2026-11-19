// One authoritative classifier for terrain, environment palettes and world fog.
// The camera altitude is deliberately not an input: climate is sampled on XZ.
#ifndef ENDURANCE_FIELD_CLIMATE_GLSL
#define ENDURANCE_FIELD_CLIMATE_GLSL
#include "field_weights.glsl"
#include "../Noise/climate_2d.glsl"

FieldColumn fieldClimateColumn(vec2 world)
{
    FieldColumn column;
    column.terrain=vec4(0.0,0.5,0.5,0.0);
    column.blend=vec4(0.0,uBandOffset,0.0,0.0);
    column.detail=uvec4(0u,0u,0u,floatBitsToUint(uBaseHeight));
    if(uFieldMode==0u)
    {
        float macroHeight=uBaseHeight;
        if(uHeightAmplitude>0.0)
            macroHeight+=noiseSimplexFbm2D(world*uHeightFrequency, uSeed ^ (0x13198a2eu), 5, 2.0, 0.5)*uHeightAmplitude;
        vec2 climate=noiseClimate2D(world,uSeed);
        float coast=(macroHeight-uSeaLevel)/max(uSeaBlendWidth,0.001)+0.5;
        vec3 grid=vec3(clamp(climate,0.0,1.0)*4.0,clamp(coast,0.0,4.0));
        uvec3 cell=min(uvec3(floor(grid)),uvec3(3u));
        uint index=cell.x+4u*cell.y+16u*cell.z;
        uvec4 mapping=fieldBiomeCells[index];
        column.terrain.yz=climate;
        column.blend.x=noiseFadeQuintic(grid.z-float(cell.z));
        column.detail=uvec4(mapping.x|(index<<20u),mapping.yz,floatBitsToUint(macroHeight));
        if(fieldForcedBiome())
        {
            uint biome=uint(uBiomeOverride);
            column.detail.yz=fieldRepeatedVolumeID(biome);
            if(biome<FIELD_MARINE_BIOME)
                column.terrain.yz=vec2(biome%5u,biome/5u)*0.25;
        }
        if(uBandOffsetVariation>0.0)
            column.blend.y+=uBandOffsetVariation*noisePerlin2D(world*0.0004, uSeed ^ (0xa4093822u));
    }
    return column;
}
#endif
