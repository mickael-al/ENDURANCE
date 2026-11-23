#version 450 core
layout(local_size_x=256,local_size_y=1,local_size_z=1) in;
#include "particle_volume.glsl"
layout(std430,binding=7) readonly buffer InstanceGroup { uint data[]; } instance;
uniform uvec3 u_instance;
uniform float u_dt;
void main()
{
    uint invocation=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*65536u+gl_GlobalInvocationID.z*16777216u;
    if(u_instance.x==0u || invocation>=u_instance.x*u_instance.z) return;
    uint id=invocation%u_instance.x;
    uint group=instance.data[u_instance.y+invocation/u_instance.x];
    uint sid=instance.data[group],base=instance.data[group+2u]+id*8u;
    vec3 center=envCenter(sid),extent=envExtent(id,sid);
    vec3 position=envVec(base),relative=(position-center)/extent;
    float remaining=particle.data[base+5u];
    float verticalLimit=extent.y+envVerticalRange(sid);
    if(remaining<=u_dt || dot(relative.xz,relative.xz)>1.0 || abs(position.y-center.y)>verticalLimit)
    { envSpawn(id,sid,base,false); return; }
    float life=envLife(id,sid),age=max(life-remaining,0.0);
    float response=max(particle.data[sid+26u],.25);
    float dt=max(u_dt,0.0);
    vec3 transient=envVec(sid+23u)+envVec(sid+20u)/response;
    // Analytic displacement under bounded drag. Velocities in the packed
    // record are outputs for streak orientation, never an integration input.
    float decayIntegral=exp(-response*age)*(1.0-exp(-response*dt))/response;
    position+=(envLaunch(id,sid)+transient)*dt-transient*decayIntegral;
    float scale=float(floatBitsToUint(particle.data[base+6u])&65535u)*(64.0/65535.0);
    envWrite(id,sid,base,position,remaining-dt,scale);
}
