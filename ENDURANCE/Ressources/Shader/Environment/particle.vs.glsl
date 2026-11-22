#version 450 core
#ifndef GE_BIOME_PARTICLE_SHAPE
#define GE_BIOME_PARTICLE_SHAPE 0
#endif

layout(std430,binding=0) readonly buffer UniformBufferCamera
{ vec3 camPos; mat4 view; mat4 invView; mat4 proj; } ubc;
layout(std430,binding=3) readonly buffer UniformBufferDivers
{ int maxLight; float u_time; float gamma; float ambiant; float fov; bool ortho; } ubd;
layout(std430,binding=6) readonly buffer ParticleData { float data[]; } particle;
layout(std430,binding=7) readonly buffer InstanceGroup { uint data[]; } instance;
uniform uvec2 u_count;
out VS_OUT
{
    vec2 uv;
    vec4 color;
    vec3 worldPosition;
    float viewDepth;
    flat float emission;
    flat float variation;
} vs_out;

float random01(uint value)
{
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return float(value ^ (value >> 16)) * (1.0 / 4294967295.0);
}
vec4 readColor(uint base)
{
    uint a=floatBitsToUint(particle.data[base+3]);
    uint b=floatBitsToUint(particle.data[base+4]);
    return vec4(a&65535u,a>>16,b&65535u,b>>16)*(64.0/65535.0);
}

void main()
{
    // Dead/waiting particles have a well-defined clipped position. The generic
    // particle stream uses three vertices per sprite, no vertex buffer.
    gl_Position=vec4(2.0,2.0,2.0,1.0);
    vs_out.uv=vec2(0); vs_out.color=vec4(0);
    vs_out.worldPosition=vec3(0); vs_out.viewDepth=0;
    vs_out.emission=0; vs_out.variation=0;
    uint id=uint(gl_VertexID)/3u;
    if(id>=u_count.x) return;
    uint group=instance.data[u_count.y+uint(gl_InstanceID)];
    uint sid=instance.data[group];
    uint base=instance.data[group+2u]+id*8u;
    uint packedScale=floatBitsToUint(particle.data[base+6]);
    float scale=float(packedScale&65535u)*(64.0/65535.0);
    float strength=particle.data[sid+93u];
    if(scale<=0.00001 || strength<=0.0 || particle.data[base+5u]<=0.0) return;
    vec3 position=vec3(particle.data[base],particle.data[base+1u],particle.data[base+2u]);
    float seed=random01(id+sid*1664525u);
    float phase=seed*6.28318530718+particle.data[sid+100u];
    float time=ubd.u_time;
    // Coherent vertex-only flutter: no additional simulation dispatch or 3D
    // procedural field. Every sprite's three vertices share the displacement.
    float turbulence=particle.data[sid+97u];
    float frequency=particle.data[sid+98u];
    vec2 drift=sin(position.xz*frequency+vec2(time*.72+phase,time*.47-phase));
    position+=vec3(drift.x,sin(time*.61+phase)*.22,drift.y)*turbulence;
    vec3 right=ubc.invView[0].xyz;
    vec3 up=ubc.invView[1].xyz;
#if GE_BIOME_PARTICLE_SHAPE == 2
    // Rain/seed/meteor streaks follow the projected world velocity, including
    // a tilted camera, instead of facing sideways or spinning at the poles.
    uint packed2=floatBitsToUint(particle.data[base+7u]);
    vec3 velocity=(vec3(packedScale>>16,packed2&65535u,packed2>>16)/32767.5-1.0)*256.0;
    vec2 axis=(mat3(ubc.view)*velocity).xy;
    axis=dot(axis,axis)>1e-8?normalize(axis):vec2(0,1);
    vec3 oldRight=right;
    right=oldRight*axis.y-up*axis.x;
    up=oldRight*axis.x+up*axis.y;
#else
    float angle=phase;
    uint flags=floatBitsToUint(particle.data[sid+9u]);
    if((flags&(1u<<6u))!=0u) angle+=time*particle.data[sid+83u];
    float c=cos(angle),s=sin(angle);
    vec3 oldRight=right;
    right=oldRight*c+up*s; up=up*c-oldRight*s;
#endif
    const vec2 corners[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    vec2 corner=corners[uint(gl_VertexID)%3u];
    float aspect=clamp(particle.data[sid+95u],.05,30.0);
    vec3 world=position+(right*corner.x+up*corner.y*aspect)*scale;
    vec4 view=ubc.view*vec4(world,1);
    vs_out.uv=corner*.5+.5;
    vs_out.worldPosition=world;
    vs_out.viewDepth=-view.z;
    vs_out.color=readColor(base);
    // Large vapor sheets fade before filling the camera, while fine snow and
    // pollen retain the short near fade. This bounds close-up alpha overdraw.
    float spriteRadius=scale*max(aspect,1.0);
    float nearFade=smoothstep(max(.15,spriteRadius*.6),max(.85,spriteRadius*1.6),length(position-ubc.camPos));
    // Pulsation varies by particle, retaining bright firefly/ember cores.
    float pulse=max(particle.data[sid+99u],0.0);
    float pulseMix=clamp(pulse,0.0,1.0);
    float blink=mix(1.0,.3+.7*pow(.5+.5*sin(time*pulse+phase),2.0),pulseMix);
    vs_out.color.a=clamp(vs_out.color.a*strength*nearFade*blink,0.0,1.0);
    vs_out.emission=max(particle.data[sid+94u],0.0);
    vs_out.variation=seed;
    gl_Position=ubc.proj*view;
}
