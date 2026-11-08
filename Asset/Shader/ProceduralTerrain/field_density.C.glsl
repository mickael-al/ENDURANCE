#version 450 core
// Scalar diagnostic/reference entry point. Runtime terrain uses
// field_batch_density.C.glsl followed by the 26 guarded volume specializations.
#include "field_volume.glsl"
layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) writeonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { FieldColumn columns[]; };
void main()
{
    uvec3 p=gl_GlobalInvocationID;
    uint side=uCells+3u;
    if(any(greaterThanEqual(p,uvec3(side)))) return;
    density[p.x+p.y*side+p.z*side*side]=fieldDensityAt(
        fieldWorldPosition(ivec3(p)),columns[p.x+p.z*side]);
}
