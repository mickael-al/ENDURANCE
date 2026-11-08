#version 450 core
#include "field_batch_common.glsl"
#include "field_weights.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) readonly buffer FieldColumns { PackedFieldColumn columns[]; };
shared uvec2 localMasks[64];
shared vec2 localEnvelopes[64];

void main()
{
    uint job=gl_WorkGroupID.z;
    if(!fieldJobHas(job,FIELD_JOB_BUILD_DENSITY)) return;
    fieldSelectJob(job);
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    uvec2 masks=uvec2(0u); // possible positive/nonpositive signs, volume biomes
    vec2 envelopeRange=vec2(3.402823e38,-3.402823e38);
    if(all(lessThan(p,uvec2(side))))
    {
        FieldColumn column=fieldUnpackColumn(columns[fieldJobs[job].storage.x*uColumnStride+p.x+p.y*side]);
        // The entire [-1,N+1] halo participates, including normal/LOD samples.
        vec2 yRange=vec2(fieldWorldPosition(ivec3(1,0,1)).y,
                         fieldWorldPosition(ivec3(1,int(side-1u),1)).y);
        masks.x=uint(column.terrain.x>=yRange.x)|(uint(column.terrain.x<=yRange.y)<<1u);
        if(uFieldMode==2u) masks.x=3u; // no height-only exclusion for spheres
        if(uFieldMode==0u && uDetailScale>0.0 && column.terrain.w>0.0002)
        {
            uint biomes=fieldVolumeMask(column);
            vec2 envelope=fieldVolumeEnvelope(column);
            if(biomes!=0u && yRange.y>=envelope.x && yRange.x<=envelope.y)
            {
                masks.y=biomes;
                masks.x=3u; // a free 3D recipe can change either sign in its envelope
                envelopeRange=envelope;
            }
        }
    }
    uint lane=gl_LocalInvocationIndex;
    localMasks[lane]=masks;
    localEnvelopes[lane]=envelopeRange;
    barrier();
    for(uint step=32u;step>0u;step>>=1u)
    {
        if(lane<step)
        {
            localMasks[lane]|=localMasks[lane+step];
            localEnvelopes[lane]=vec2(min(localEnvelopes[lane].x,localEnvelopes[lane+step].x),
                                      max(localEnvelopes[lane].y,localEnvelopes[lane+step].y));
        }
        barrier();
    }
    if(lane==0u)
    {
        uint header=fieldJobs[job].storage.z*uStateStride;
        uint groupsY=(side+3u)/4u;
        uint first=groupsY,last=0u;
        if(localMasks[0].y!=0u)
        {
            // Compare actual world-space sample endpoints instead of dividing
            // large floating-point coordinates to estimate voxel indices. The
            // monotone lattice and existing padded envelope make this bound
            // conservative at large origins and across LOD boundaries.
            for(uint group=0u;group<groupsY;++group)
            {
                float lo=fieldWorldPosition(ivec3(1,int(group*4u),1)).y;
                float hi=fieldWorldPosition(ivec3(1,int(min(group*4u+3u,side-1u)),1)).y;
                if(hi>=localEnvelopes[0].x && lo<=localEnvelopes[0].y)
                {
                    first=min(first,group);
                    last=group+1u;
                }
            }
        }
        uint tile=gl_WorkGroupID.x+gl_WorkGroupID.y*((side+7u)/8u);
        fieldStates[header+1u+tile]=uvec4(localMasks[0].y,first,last,0u);
        if(localMasks[0].x!=0u) atomicOr(fieldStates[header].x,localMasks[0].x);
        if(last>first)
        {
            atomicOr(fieldJobs[job].info.w,localMasks[0].y);
            atomicMin(fieldStates[header].y,first);
            atomicMax(fieldStates[header].z,last);
        }
    }
}
