#version 450 core
#include "world_field.glsl"

layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) writeonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { FieldColumn columns[]; };

// The four invocations in Y share their column data. Each 64-byte column is
// loaded once per group, rather than once per voxel.
shared FieldColumn cachedColumns[32];

void main()
{
    uvec3 p=gl_GlobalInvocationID;
    uint side=uCells+3u;
    uint localColumn=gl_LocalInvocationID.x+gl_LocalInvocationID.z*8u;
    if(gl_LocalInvocationID.y==0u && p.x<side && p.z<side)
        cachedColumns[localColumn]=columns[p.x+p.z*side];
    barrier();
    if(any(greaterThanEqual(p,uvec3(side)))) return;
    density[p.x+p.y*side+p.z*side*side]=fieldDensityAt(fieldWorldPosition(ivec3(p)),cachedColumns[localColumn]);
}
