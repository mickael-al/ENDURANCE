#version 450 core
#include "field_batch_common.glsl"
#include "field_weights.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) readonly buffer FieldScratch { FieldColumn scratchColumns[]; };
layout(std430,binding=2) writeonly buffer FinalColumns { PackedFieldColumn columns[]; };
void main()
{
    uint job=gl_WorkGroupID.z;
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    if(!fieldJobHas(job,FIELD_JOB_BUILD_COLUMNS) || any(greaterThanEqual(p,uvec2(side)))) return;
    fieldSelectJob(job);
    uint sampleIndex=p.x+p.y*side;
    FieldColumn column=scratchColumns[fieldJobs[job].storage.y*uColumnStride+sampleIndex];
    vec4 relief=clamp(vec4(column.terrain.x,column.terrain.w,column.blend.zw),
        vec4(-FIELD_MAX_RELIEF_HEIGHT,0.0,-160.0,0.0),vec4(FIELD_MAX_RELIEF_HEIGHT,FIELD_MAX_VOLUME_THICKNESS,160.0,1.0));
    precise float height=uintBitsToFloat(column.detail.w)+relief.x*uDetailScale;
    column.terrain.x=height;
    column.terrain.w=relief.y*uDetailScale;
    column.blend.zw=relief.zw;
    columns[fieldJobs[job].storage.x*uColumnStride+sampleIndex]=fieldPackColumn(column,relief.y);
}
