#version 460 core
#include "common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=1) readonly buffer SelectedBlocks { uint selectedBlocks[]; };
struct Command { uint count; uint instanceCount; uint firstIndex; int baseVertex; uint baseInstance; };
layout(std430,binding=2) writeonly buffer DrawCommands { Command commands[]; };
layout(std430,binding=4) readonly buffer TreeInstances { TreeInstance instances[]; };
layout(std430,binding=7) writeonly buffer VisibleInstances { uvec2 visible[]; };
uniform vec4 uPlanes[6];
uniform vec3 uCamera;
uniform vec4 uLod; // projected LOD0, projected impostor, transition, max distance
uniform float uProjectionScale;
uniform bool uOrthographic;
shared uint counts[3];

void appendInstance(uint lod,uint instance,float fineWeight,bool coarse,float distanceWeight)
{
    uint destination=atomicAdd(counts[lod],1u);
    uint fade=uint(round(clamp(fineWeight,0.0,1.0)*255.0)) | (coarse?256u:0u)
        | (uint(round(clamp(distanceWeight,0.0,1.0)*127.0))<<9u);
    visible[(gl_WorkGroupID.x*3u+lod)*64u+destination]=uvec2(instance,fade);
}
void main()
{
    uint lane=gl_LocalInvocationID.x;
    if(lane<3u) counts[lane]=0u;
    barrier();
    TreeBlock block=blocks[selectedBlocks[gl_WorkGroupID.x]];
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
            float projected=radius*uProjectionScale/(uOrthographic?1.0:max(distance,0.01));
            float farWeight=1.0-smoothstep(uLod.w*0.9,uLod.w,distance);
            float f=uLod.z;
            if(projected>uLod.x*(1.0+f)) appendInstance(0u,id,1.0,false,farWeight);
            else if(f>0.0 && projected>uLod.x*(1.0-f))
            {
                float weight=smoothstep(uLod.x*(1.0-f),uLod.x*(1.0+f),projected);
                appendInstance(0u,id,weight,false,farWeight); appendInstance(1u,id,weight,true,farWeight);
            }
            else if(model.scalesImpostor.w<0.5 || projected>uLod.y*(1.0+f)) appendInstance(1u,id,1.0,false,farWeight);
            else if(f>0.0 && projected>uLod.y*(1.0-f))
            {
                float weight=smoothstep(uLod.y*(1.0-f),uLod.y*(1.0+f),projected);
                appendInstance(1u,id,weight,false,farWeight); appendInstance(2u,id,weight,true,farWeight);
            }
            else appendInstance(2u,id,1.0,false,farWeight);
        }
    }
    barrier();
    if(lane<5u)
    {
        uint lod=lane<4u?lane/2u:2u;
        uint vertices=lane<4u?model.counts[lane]:(model.scalesImpostor.w>0.5?6u:0u);
        commands[gl_WorkGroupID.x*5u+lane]=Command(vertices,counts[lod],0u,0,(gl_WorkGroupID.x*3u+lod)*64u);
    }
}
