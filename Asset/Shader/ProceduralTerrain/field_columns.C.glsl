#version 450 core
#include "field_surface.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) writeonly buffer FieldColumns { FieldColumn columns[]; };
void main()
{
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    if(any(greaterThanEqual(p,uvec2(side)))) return;
    uint address=p.x+p.y*side;
    vec3 world=fieldWorldPosition(ivec3(p.x,1,p.y));
    // Climate, matrix lookup and relief stay in this invocation. Only the
    // completed column is written for reuse by the volume and meshing passes.
    columns[address]=fieldColumnAt(world.xz);
}
