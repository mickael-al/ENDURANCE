#version 450 core
#include "field_batch_common.glsl"
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
void main()
{
    uint pass=gl_GlobalInvocationID.x;
    if(pass>=FIELD_PASS_COUNT) return;
    bool volume=pass>=FIELD_BIOME_COUNT;
    uint biome=pass%FIELD_BIOME_COUNT;
    uint count=0u;
    // One invocation owns a whole row, so lists are compact, deterministic and
    // bounded by FIELD_MAX_JOBS. They never require a CPU mask readback.
    for(uint job=0u;job<min(uJobSlots,FIELD_MAX_JOBS);++job)
    {
        uint mask=volume ? fieldJobs[job].info.w : fieldJobs[job].info.z;
        if(fieldJobs[job].info.y!=0u && (mask&(1u<<biome))!=0u)
            fieldWorkItems[pass*FIELD_MAX_JOBS+count++]=job;
    }
    uint side=uCells+3u;
    fieldCommands[pass]=uvec4((side+7u)/8u,
        volume ? (side+3u)/4u : (side+7u)/8u,
        count*(volume ? (side+3u)/4u : 1u),count);
}
