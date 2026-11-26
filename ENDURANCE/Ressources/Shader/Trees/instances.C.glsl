#version 460 core
#include "common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=2) readonly buffer PackedPositions { uvec2 positions[]; };
layout(std430,binding=4) writeonly buffer TreeInstances { TreeInstance instances[]; };
void main()
{
    TreeBlock block=blocks[gl_WorkGroupID.x];
    uint i=gl_LocalInvocationID.x;
    if(i>=block.info.y) return;
    uvec2 encoded=positions[block.info.x+i];
    uint h=treeHash((encoded.y>>16u)^treeHash(uint(block.anchor.x)^treeHash(uint(block.anchor.y)
        ^treeHash(uint(block.anchor.z)^uint(block.anchor.w)))));
    vec3 q=vec3(encoded.x&65535u,encoded.x>>16u,encoded.y&65535u);
    float angle=6.28318530718*treeRandom(h^0x73519aefu);
    TreeInstance value;
    value.positionScale=vec4(block.originExtent.xyz+q*(block.originExtent.w/65535.0),
        mix(models[block.info.z].scalesImpostor.x,models[block.info.z].scalesImpostor.y,treeRandom(h^0xa53ba921u)));
    value.rotation=vec2(cos(angle),sin(angle)); value.hash=h; value.model=block.info.z;
    instances[block.info.w+i]=value;
}
