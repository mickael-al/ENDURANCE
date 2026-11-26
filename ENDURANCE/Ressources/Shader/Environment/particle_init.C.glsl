#version 450 core
layout(local_size_x=256,local_size_y=1,local_size_z=1) in;
#define ENVIRONMENT_PARTICLE_BINDING 0
#include "particle_volume.glsl"
uniform uint u_offset,u_offsetSettings,u_count;
void main()
{
    uint id=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*65536u+gl_GlobalInvocationID.z*16777216u;
    if(id>=u_count) return;
    envSpawn(id,u_offsetSettings,u_offset+id*8u,true);
}
