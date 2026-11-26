// Game-only particle field; the engine's legacy particle ABI is unchanged.
// xyz is relative to the stable anchor in group.target (words31..33).
#ifndef ENVIRONMENT_PARTICLE_BINDING
#define ENVIRONMENT_PARTICLE_BINDING 6
#endif
layout(std430,binding=ENVIRONMENT_PARTICLE_BINDING) buffer EnvironmentParticleData { float data[]; } particle;
layout(std430,binding=3) readonly buffer UniformBufferDivers
{ int maxLight; float u_time; float gamma; float ambiant; float fov; bool ortho; } ubd;

uint envHash(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
float envRandom(inout uint seed)
{
    seed=envHash(seed+0x9e3779b9u);
    return float(seed>>8)*(1.0/16777216.0);
}
uint envSeed(uint id,uint sid)
{ return envHash(id*747796405u+floatBitsToUint(particle.data[sid+100u])); }
vec3 envVec(uint index)
{ return vec3(particle.data[index],particle.data[index+1u],particle.data[index+2u]); }
float envLife(uint id,uint sid)
{
    uint seed=envSeed(id,sid)^0x16ca34d9u;
    return max(.1,mix(particle.data[sid],particle.data[sid+1u],envRandom(seed)));
}
float envBand(uint id,uint sid)
{
    // A nested field keeps foreground detail while populating the whole view.
    // 35% nearby, 40% middle distance and 25% throughout the full outer extent.
    // The layers overlap and fade at their edges; they never form a hard ring.
    uint seed=envSeed(id,sid)^0xa3c59ac3u;
    float bucket=envRandom(seed);
    return bucket<.35?.12:(bucket<.75?.42:1.0);
}
vec3 envCenter(uint sid)
{ return envVec(sid+2u)-envVec(sid+31u)+.5*(envVec(sid+14u)+envVec(sid+17u)); }
vec3 envExtent(uint id,uint sid)
{
    vec3 extent=max(.5*abs(envVec(sid+17u)-envVec(sid+14u)),vec3(.01));
    extent.xz*=envBand(id,sid);
    return extent;
}
vec3 envLaunch(uint id,uint sid)
{
    uint seed=envSeed(id,sid)^0x8f12ac31u;
    float speed=mix(particle.data[sid+10u],particle.data[sid+11u],envRandom(seed));
    vec3 axis=envVec(sid+5u);
    axis=dot(axis,axis)>1e-8?normalize(axis):vec3(0,1,0);
    vec3 up=abs(axis.z)<.99?vec3(0,0,1):vec3(1,0,0);
    vec3 tangent=normalize(cross(up,axis)),bitangent=cross(axis,tangent);
    float ct=mix(1.0,cos(radians(clamp(particle.data[sid+8u],0.0,180.0))),envRandom(seed));
    float st=sqrt(max(1.0-ct*ct,0.0)),phi=6.28318530718*envRandom(seed);
    return speed*(axis*ct+st*(tangent*cos(phi)+bitangent*sin(phi)));
}
vec3 envVelocity(uint id,uint sid,float age)
{
    // Wind is an air velocity, not a force integrated without limit. Gravity
    // also approaches a terminal contribution. Re-evaluate from age rather
    // than feeding quantized16-bit velocity back into the next step.
    float response=max(particle.data[sid+26u],.25);
    vec3 terminal=envVec(sid+23u)+envVec(sid+20u)/response;
    return envLaunch(id,sid)+terminal*(1.0-exp(-response*max(age,0.0)));
}
vec4 envColor(uint sid,float phase)
{
    uint count=clamp(floatBitsToUint(particle.data[sid+36u]),2u,10u);
    float position=clamp(phase,0.0,1.0)*float(count-1u);
    uint a=min(uint(position),count-1u),b=min(a+1u,count-1u);
    uint ia=sid+37u+4u*a,ib=sid+37u+4u*b;
    return mix(vec4(envVec(ia),particle.data[ia+3u]),vec4(envVec(ib),particle.data[ib+3u]),fract(position));
}
void envWriteColor(uint base,vec4 color)
{
    uvec4 q=uvec4(clamp(color/64.0,0.0,1.0)*65535.0+.5);
    particle.data[base+3u]=uintBitsToFloat(q.x|(q.y<<16));
    particle.data[base+4u]=uintBitsToFloat(q.z|(q.w<<16));
}
void envWriteVelocity(uint base,vec3 velocity,float scale)
{
    uvec3 v=uvec3((clamp(velocity/256.0,-1.0,1.0)+1.0)*32767.5+.5);
    uint s=uint(clamp(scale/64.0,0.0,1.0)*65535.0+.5);
    particle.data[base+6u]=uintBitsToFloat(s|(v.x<<16));
    particle.data[base+7u]=uintBitsToFloat(v.y|(v.z<<16));
}
float envEdgeFade(uint id,uint sid,vec3 position)
{
    vec3 normalized=(position-envCenter(sid))/envExtent(id,sid);
    float horizontal=length(normalized.xz);
    // Y is a birth layer, not a cage: smoke and embers must be able to rise
    // above it, and precipitation must fall below it for the full lifetime.
    return 1.0-smoothstep(.78,1.0,horizontal);
}
float envVerticalRange(uint sid)
{
    float response=max(particle.data[sid+26u],.25);
    float speed=max(abs(particle.data[sid+10u]),abs(particle.data[sid+11u]));
    float terminal=abs(particle.data[sid+24u])+abs(particle.data[sid+21u])/response;
    return max(particle.data[sid],particle.data[sid+1u])*(speed+terminal)+.5;
}
void envWrite(uint id,uint sid,uint base,vec3 position,float remaining,float scale)
{
    // Complete all settings reads before touching the shared particle buffer.
    // This also permits the compiler to reuse envLife/envLaunch/envExtent from
    // the caller without conservatively treating the stores as aliases.
    float life=envLife(id,sid),age=max(life-remaining,0.0);
    vec4 color=envColor(sid,age/life);
    color.a*=envEdgeFade(id,sid,position);
    vec3 velocity=envVelocity(id,sid,age);
    particle.data[base]=position.x;particle.data[base+1u]=position.y;particle.data[base+2u]=position.z;
    particle.data[base+5u]=remaining;
    envWriteColor(base,color);
    envWriteVelocity(base,velocity,scale);
}
void envSpawn(uint id,uint sid,uint base,bool prewarm)
{
    uint seed=envSeed(id,sid)^0x6b43c971u;
    // Each rebirth changes its point, rather than repeating the same tracks.
    if(!prewarm) seed^=envHash(floatBitsToUint(ubd.u_time));
    float radius=sqrt(envRandom(seed)),angle=6.28318530718*envRandom(seed);
    vec3 samplePoint=vec3(radius*cos(angle),envRandom(seed)*2.0-1.0,radius*sin(angle));
    vec3 position=envCenter(sid)+samplePoint*envExtent(id,sid);
    float scale=mix(particle.data[sid+12u],particle.data[sid+13u],envRandom(seed));
    float life=envLife(id,sid);
    // Initialize a steady, age-distributed field, not a synchronized explosion
    // at the player's feet. Subsequent births start transparent and fade in.
    float remaining=life*(prewarm?mix(.08,.92,envRandom(seed)):1.0);
    if(prewarm)
    {
        float age=life-remaining,response=max(particle.data[sid+26u],.25);
        float terminal=particle.data[sid+24u]+particle.data[sid+21u]/response;
        position.y+=envLaunch(id,sid).y*age+terminal*(age-(1.0-exp(-response*age))/response);
    }
    envWrite(id,sid,base,position,remaining,scale);
}
