#ifndef ENDURANCE_FIELD_BATCH_COMMON_GLSL
#define ENDURANCE_FIELD_BATCH_COMMON_GLSL
#define FIELD_BATCHED 1
#include "field_common.glsl"

// Keep these layouts in sync with ProceduralTerrainGpu.cpp. The work lists
// contain chunk slots, not copies of their columns or density fields.
const uint FIELD_MAX_JOBS=16u;
const uint FIELD_PASS_COUNT=52u;
const uint FIELD_JOB_ACTIVE=1u;
const uint FIELD_JOB_BUILD_COLUMNS=2u;
const uint FIELD_JOB_BUILD_DENSITY=4u;
#ifndef FIELD_MATERIALIZE_HOMOGENEOUS
#define FIELD_MATERIALIZE_HOMOGENEOUS 0
#endif
struct FieldJob
{
    ivec4 originLod;
    uvec4 info; // seed, flags, surface mask, volume mask
    uvec4 storage; // cached column slot, FP32 scratch slot, cached density slot, reserved
};
layout(std430,binding=6) buffer FieldJobs { FieldJob fieldJobs[]; };
layout(std430,binding=3) buffer FieldStates { uvec4 fieldStates[]; };
layout(std430,binding=5) buffer FieldWorkQueue
{
    uvec4 fieldCommands[52]; // indirect XYZ group counts, number of chunks
    uint fieldWorkItems[832]; // [52 passes][16 chunk slots]
};
uniform uint uJobSlots;
uniform uint uColumnStride;  // record count, aligned for BOTH 48B scratch and 32B final
uniform uint uDensityStride; // float elements, including alignment padding
uniform uint uStateStride;   // uvec4 header + one uvec4 per 8x8 XZ tile

bool fieldJobHas(uint job,uint flag)
{
    return job<uJobSlots && (fieldJobs[job].info.y&(FIELD_JOB_ACTIVE|flag))==(FIELD_JOB_ACTIVE|flag);
}

bool fieldJobHasSurface(uint job)
{
    return fieldStates[fieldJobs[job].storage.z*uStateStride].x==3u;
}

void fieldSelectJob(uint job)
{
    uOrigin=fieldJobs[job].originLod.xyz;
    uLod=uint(fieldJobs[job].originLod.w);
    uSeed=fieldJobs[job].info.x;
}

// Z groups concatenate complete volumes. Padding belongs to each chunk and
// must never spill into the next chunk's slice of either arena.
void fieldVolumeCoordinates(out uint listIndex,out uvec3 p)
{
    uint groupsZ=(uCells+6u)/4u;
    listIndex=gl_WorkGroupID.z/groupsZ;
    p=uvec3(gl_GlobalInvocationID.xy,
        (gl_WorkGroupID.z%groupsZ)*4u+gl_LocalInvocationID.z);
}

#endif
