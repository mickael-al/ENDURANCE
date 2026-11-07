#version 450 core
#define FIELD_CLASSIFY_PASS 1
#include "world_field.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) writeonly buffer FieldColumns { FieldColumn columns[]; };
layout(std430,binding=3) buffer ClassificationCounters
{
    uint total; uint cursor; uint overflow; uint padding;
    uint cellsLo; uint cellsHi; uint pad0; uint pad1;
};
shared uint groupCellsLo;
shared uint groupCellsHi;
void main()
{
    if(gl_LocalInvocationIndex==0u) { groupCellsLo=0u; groupCellsHi=0u; }
    barrier();
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    if(all(lessThan(p,uvec2(side))))
    {
        vec3 world=fieldWorldPosition(ivec3(p.x,1,p.y));
        FieldColumn column=fieldClassifyColumn(world.xz);
        columns[p.x+p.y*side]=column;
        uint cell=column.detail.w;
        if(cell<32u) atomicOr(groupCellsLo,1u<<cell);
        else atomicOr(groupCellsHi,1u<<(cell-32u));
    }
    barrier();
    if(gl_LocalInvocationIndex==0u)
    {
        if(groupCellsLo!=0u) atomicOr(cellsLo,groupCellsLo);
        if(groupCellsHi!=0u) atomicOr(cellsHi,groupCellsHi);
    }
}
