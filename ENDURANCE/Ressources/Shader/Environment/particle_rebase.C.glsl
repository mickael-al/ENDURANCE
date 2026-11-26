#version 450 core
layout(local_size_x=256,local_size_y=1,local_size_z=1) in;
layout(std430,binding=0) buffer ParticleData { float data[]; } particle;
uniform uint u_offset,u_count;
uniform vec3 uAnchorDelta;
void main()
{
    uint id=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*65536u+gl_GlobalInvocationID.z*16777216u;
    if(id>=u_count) return;
    uint base=u_offset+id*8u;
    particle.data[base]+=uAnchorDelta.x;
    particle.data[base+1u]+=uAnchorDelta.y;
    particle.data[base+2u]+=uAnchorDelta.z;
}
