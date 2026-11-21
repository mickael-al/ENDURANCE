#version 460 core
#include "common.glsl"
layout(local_size_x=64) in;
// block ID, model group ID, first selected block of that group, group block count
layout(std430,binding=1) readonly buffer SelectedBlocks { uvec4 selectedBlocks[]; };
struct Command { uint count; uint instanceCount; uint firstIndex; int baseVertex; uint baseInstance; };
layout(std430,binding=2) buffer DrawCommands { Command commands[]; };
layout(std430,binding=4) readonly buffer TreeInstances { TreeInstance instances[]; };
layout(std430,binding=7) writeonly buffer VisibleInstances { uvec2 visible[]; };
uniform vec4 uPlanes[6];
uniform vec3 uCamera;
uniform vec4 uLod; // projected LOD0, projected impostor, transition, max distance
uniform float uProjectionScale;
shared uint counts[3];
shared uint offsets[3];
shared uvec2 pageReferences[192];

void appendInstance(uint lod,uint instance,float fineWeight,bool coarse,float distanceWeight)
{
    uint destination=atomicAdd(counts[lod],1u);
    uint fade=uint(round(clamp(fineWeight,0.0,1.0)*255.0)) | (coarse?256u:0u)
        | (uint(round(clamp(distanceWeight,0.0,1.0)*127.0))<<9u);
    pageReferences[lod*64u+destination]=uvec2(instance,fade);
}
void main()
{
    uint lane=gl_LocalInvocationID.x;
    if(lane<3u) counts[lane]=0u;
    barrier();
    uvec4 selection=selectedBlocks[gl_WorkGroupID.x];
    TreeBlock block=blocks[selection.x];
    TreeModel model=models[block.info.z];
    if(lane<block.info.y)
    {
        uint id=block.info.w+lane; TreeInstance instance=instances[id];
        float scale=instance.positionScale.w;
        vec3 centre=instance.positionScale.xyz+treeRotate(model.centreRadius.xyz*scale,instance.rotation);
        float radius=model.centreRadius.w*scale;
        bool accepted=true;
        for(int p=0;p<6;++p) accepted=accepted && dot(uPlanes[p],vec4(centre,1))>=-radius;
        float distance=length(centre-uCamera);
        accepted=accepted && distance<uLod.w;
        if(accepted)
        {
#if TREE_ORTHOGRAPHIC
            float projected=radius*uProjectionScale;
#else
            float projected=radius*uProjectionScale/max(distance,0.01);
#endif
            float farWeight=1.0-smoothstep(uLod.w*0.9,uLod.w,distance);
            float f=uLod.z;
            if(projected>uLod.x*(1.0+f)) appendInstance(0u,id,1.0,false,farWeight);
#if TREE_LOD_TRANSITIONS
            else if(projected>uLod.x*(1.0-f))
            {
                float weight=smoothstep(uLod.x*(1.0-f),uLod.x*(1.0+f),projected);
                appendInstance(0u,id,weight,false,farWeight); appendInstance(1u,id,weight,true,farWeight);
            }
#endif
            else if(model.scalesImpostor.w<0.5 || projected>uLod.y*(1.0+f)) appendInstance(1u,id,1.0,false,farWeight);
#if TREE_LOD_TRANSITIONS
            else if(projected>uLod.y*(1.0-f))
            {
                float weight=smoothstep(uLod.y*(1.0-f),uLod.y*(1.0+f),projected);
                appendInstance(1u,id,weight,false,farWeight); appendInstance(2u,id,weight,true,farWeight);
            }
#endif
            else appendInstance(2u,id,1.0,false,farWeight);
        }
    }
    barrier();
    if(lane<3u)
    {
        uint command=selection.y*5u+(lane<2u?lane*2u:4u);
        offsets[lane]=atomicAdd(commands[command].instanceCount,counts[lane]);
        if(lane<2u) atomicAdd(commands[command+1u].instanceCount,counts[lane]);
    }
    barrier();
    for(uint lod=0u;lod<3u;++lod) if(lane<counts[lod])
        visible[selection.z*192u+lod*selection.w*64u+offsets[lod]+lane]=pageReferences[lod*64u+lane];
    // Exactly one workgroup writes the non-atomic fields. instanceCount was
    // cleared before this dispatch and is only modified through atomics.
    if(gl_WorkGroupID.x==selection.z && lane<5u)
    {
        uint lod=lane<4u?lane/2u:2u;
        uint vertices=lane<4u?model.counts[lane]:(model.scalesImpostor.w>0.5?6u:0u);
        uint index=selection.y*5u+lane;
        commands[index].count=vertices;
        commands[index].firstIndex=lane<4u?model.firstIndices[lane]:0u;
        commands[index].baseVertex=lane<4u?model.baseVertices[lane]:0;
        commands[index].baseInstance=selection.z*192u+lod*selection.w*64u;
    }
}
