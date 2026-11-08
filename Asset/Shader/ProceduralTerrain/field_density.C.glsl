#version 450 core
#include "field_volume.glsl"
layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) writeonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { FieldColumn columns[]; };
// 48-byte persistent columns, shared by four Y invocations. The two climate
// ratios are reconstructed once per shared column, not for every voxel.
shared FieldColumn cachedColumns[32];
shared vec2 cachedClimateRatios[32];
void main()
{
    uvec3 p=gl_GlobalInvocationID;
    uint side=uCells+3u;
    uint localColumn=gl_LocalInvocationID.x+gl_LocalInvocationID.z*8u;
    if(gl_LocalInvocationID.y==0u && p.x<side && p.z<side)
    {
        FieldColumn column=columns[p.x+p.z*side];
        cachedColumns[localColumn]=column;
        cachedClimateRatios[localColumn]=fieldColumnClimateRatio(column);
    }
    barrier();
    if(any(greaterThanEqual(p,uvec3(side)))) return;
    density[p.x+p.y*side+p.z*side*side]=fieldDensityAtPrepared(fieldWorldPosition(ivec3(p)),
        cachedColumns[localColumn],cachedClimateRatios[localColumn]);
}
