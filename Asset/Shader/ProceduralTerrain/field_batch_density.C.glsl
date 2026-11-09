#version 450 core
#include "field_batch_common.glsl"
layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) writeonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { PackedFieldColumn columns[]; };
void main()
{
    uint job;
    uvec3 p;
    fieldVolumeCoordinates(job,p);
    uint side=uCells+3u;
    if(!fieldJobHas(job,FIELD_JOB_BUILD_DENSITY)
#if !FIELD_MATERIALIZE_HOMOGENEOUS
       || !fieldJobHasSurface(job)
#endif
       || any(greaterThanEqual(p,uvec3(side)))) return;
    fieldSelectJob(job);
    vec3 world=fieldWorldPosition(ivec3(p));
    float value=columns[fieldJobs[job].storage.x*uColumnStride+p.x+p.z*side].height-world.y;
    if(uFieldMode==1u) value=uBaseHeight-world.y;
    if(uFieldMode==2u) value=uSphereRadius-length(world-uSphereCenter);
    density[fieldJobs[job].storage.z*uDensityStride+p.x+p.y*side+p.z*side*side]=value;
}
