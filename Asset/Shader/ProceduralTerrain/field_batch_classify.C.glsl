#version 450 core
#include "field_batch_common.glsl"
#include "field_climate.glsl"
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
        FieldColumn column=fieldClimateColumn(world);
        if(uFieldMode==0u && uDetailScale>0.0) mask=fieldSurfaceMask(column);
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
