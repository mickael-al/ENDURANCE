#version 450 core
layout(local_size_x=1,local_size_y=1,local_size_z=1) in;
layout(std430,binding=3) buffer MeshCounters { uint total; uint cursor; uint overflow; uint padding; };
layout(std430,binding=5) writeonly buffer DrawCommands { uint commands[]; };
uniform uint uChunkSlot;
uniform uint uVertexFirst;
uniform uint uVertexCapacity;
void main()
{
    if(cursor!=total || cursor>uVertexCapacity) atomicOr(overflow,2u);
    uint p=uChunkSlot*4u;
    commands[p]=overflow==0u?cursor:0u;
    commands[p+1u]=1u;
    commands[p+2u]=uVertexFirst;
    commands[p+3u]=uChunkSlot;
}
