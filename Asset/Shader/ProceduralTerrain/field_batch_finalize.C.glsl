#version 450 core
#include "field_batch_common.glsl"
#include "field_weights.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) buffer FieldColumns { FieldColumn columns[]; };
void main()
{
    uint job=gl_WorkGroupID.z;
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    if(job>=uJobSlots || fieldJobs[job].info.y==0u || any(greaterThanEqual(p,uvec2(side)))) return;
    fieldSelectJob(job);
    uint address=job*uColumnStride+p.x+p.y*side;
    FieldColumn column=columns[address];
    vec4 relief=clamp(vec4(column.terrain.x,column.terrain.w,column.blend.zw),
        vec4(-640.0,0.0,-160.0,0.0),vec4(640.0,576.0,160.0,1.0));
    precise float height=uintBitsToFloat(column.detail.w)+relief.x*uDetailScale;
    column.terrain.x=height;
    column.terrain.w=relief.y*uDetailScale;
    column.blend.zw=relief.zw;
    column.detail.w=uFieldMode==0u ? fieldVolumeMask(column) : 0u;
    columns[address]=column;
    if(uFieldMode==0u && uDetailScale>0.0 && column.terrain.w>0.0002)
    {
        // The full [-1,N+1] halo supplies normals and transition samples.
        // Classifying only the core or a chunk average would miss contributors.
        vec2 yRange=vec2(fieldWorldPosition(ivec3(1,0,1)).y,
                         fieldWorldPosition(ivec3(1,int(side-1u),1)).y);
        vec2 envelope=fieldVolumeEnvelope(column);
        if(yRange.y>=envelope.x && yRange.x<=envelope.y)
            atomicOr(fieldJobs[job].info.w,column.detail.w);
    }
}
