#version 450 core
#include "field_batch_common.glsl"
#include "field_weights.glsl"
#include "../Noise/climate_2d.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) writeonly buffer FieldColumns { FieldColumn columns[]; };
shared uint localMasks[64];

void main()
{
    uint job=gl_WorkGroupID.z;
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    if(!fieldJobHas(job,FIELD_JOB_BUILD_COLUMNS)) return;
    fieldSelectJob(job);
    bool valid=all(lessThan(p,uvec2(side)));
    uint mask=0u;
    if(valid)
    {
        vec2 world=fieldWorldPosition(ivec3(p.x,1,p.y)).xz;
        FieldColumn column;
        column.terrain=vec4(0.0,0.5,0.5,0.0);
        column.blend=vec4(0.0,uBandOffset,0.0,0.0);
        column.detail=uvec4(0u,0u,0u,floatBitsToUint(uBaseHeight));
        if(uFieldMode==0u)
        {
            float macroHeight=uBaseHeight;
            if(uHeightAmplitude>0.0)
                macroHeight+=noiseSimplexFbm2D(world*uHeightFrequency, uSeed ^ (0x13198a2eu), 6, 2.0, 0.5)*uHeightAmplitude;
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
            if(uDetailScale>0.0)
                mask=fieldSurfaceMask(column);
        }
        // Macro height is temporarily stored losslessly in detail.w; the other
        // four relief channels start at zero and are accumulated in biome-ID order.
        columns[fieldJobs[job].storage.y*uColumnStride+p.x+p.y*side]=column;
    }
    // One global OR per nonempty workgroup, instead of one per column. Padding
    // contributes zero and participates in every barrier.
    uint lane=gl_LocalInvocationIndex;
    localMasks[lane]=mask;
    barrier();
    for(uint step=32u;step>0u;step>>=1u)
    {
        if(lane<step) localMasks[lane]|=localMasks[lane+step];
        barrier();
    }
    if(lane==0u && localMasks[0]!=0u) atomicOr(fieldJobs[job].info.z,localMasks[0]);
}
