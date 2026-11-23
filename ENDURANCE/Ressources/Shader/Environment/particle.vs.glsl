#version 450 core
#ifndef GE_BIOME_PARTICLE_KIND
#define GE_BIOME_PARTICLE_KIND 0
#endif

layout(std430,binding=0) readonly buffer UniformBufferCamera
{ vec3 camPos; mat4 view; mat4 invView; mat4 proj; } ubc;
layout(std430,binding=3) readonly buffer UniformBufferDivers
{ int maxLight; float u_time; float gamma; float ambiant; float fov; bool ortho; } ubd;
layout(std430,binding=6) readonly buffer ParticleData { float data[]; } particle;
layout(std430,binding=7) readonly buffer InstanceGroup { uint data[]; } instance;
uniform uvec2 u_count;
uniform vec2 uEnvironmentInvResolution;
out VS_OUT
{
    vec2 uv;
    vec4 color;
    vec3 worldPosition;
    float viewDepth;
    flat float emission;
    flat float variation;
    flat float aspect;
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
float softWave(float phase)
{
    float triangle=abs(fract(phase)*2.0-1.0);
    return triangle*triangle*(3.0-2.0*triangle)*2.0-1.0;
}
// Analytic derivatives of a small stream function: bounded eddy displacement,
// not a fluid solver. The two terms avoid every mote following parallel lines.
vec2 eddy(vec2 p,float t)
{
    p+=vec2(t*.23,-t*.17);
    float diagonal=cos(dot(p,vec2(1.7,-.8))+t*.11);
    return vec2(sin(p.x)*cos(p.y)-.36*diagonal,
               -cos(p.x)*sin(p.y)-.765*diagonal)*.56;
}

void main()
{
    gl_Position=vec4(2.0,2.0,2.0,1.0);
    vs_out.uv=vec2(0); vs_out.color=vec4(0);
    vs_out.worldPosition=vec3(0); vs_out.viewDepth=0;
    vs_out.emission=0; vs_out.variation=0; vs_out.aspect=1;
    uint id=uint(gl_VertexID)/3u;
    if(id>=u_count.x) return;
    uint group=instance.data[u_count.y+uint(gl_InstanceID)];
    uint sid=instance.data[group];
    uint base=instance.data[group+2u]+id*8u;
    uint packedScale=floatBitsToUint(particle.data[base+6]);
    float scale=float(packedScale&65535u)*(64.0/65535.0);
    float strength=particle.data[sid+93u];
    if(scale<=0.00001 || strength<=0.0 || particle.data[base+5u]<=0.0) return;
    // Simulation positions stay local to an immutable emitter anchor, avoiding
    // quantization of slow falling/drifting velocities far from the world origin.
    vec3 position=vec3(particle.data[base],particle.data[base+1u],particle.data[base+2u])
        +vec3(particle.data[sid+31u],particle.data[sid+32u],particle.data[sid+33u]);
    float seed=random01(id+sid*1664525u);
    float phase=seed*6.28318530718+particle.data[sid+100u];
    float time=ubd.u_time;
    float t=time*mix(.82,1.24,seed);
    float turbulence=particle.data[sid+97u];
    float frequency=particle.data[sid+98u];
    float turn=0.0,tilt=1.0,breathing=1.0;
    vec3 detail=vec3(0);
#if GE_BIOME_PARTICLE_KIND == 0
    // Mineral dust: low horizontal rolls, with short lifting/saltation arcs.
    vec2 flow=eddy(position.xz*frequency,t*.55);
    detail=vec3(flow.x,.18*softWave(t*.18+seed),flow.y);
#elif GE_BIOME_PARTICLE_KIND == 1
    // Snow: gentle independent lateral meander, barely any vertical flutter.
    detail=vec3(sin(t*.72+phase),.09*sin(t*.91-phase),cos(t*.49+phase))*.65;
    turn=.22*sin(t*.8+phase); tilt=.72+.28*abs(cos(t*.54+phase));
#elif GE_BIOME_PARTICLE_KIND == 2 || GE_BIOME_PARTICLE_KIND == 10 || GE_BIOME_PARTICLE_KIND == 12
    // Rain, sea droplets and meteors retain ballistic motion from simulation.
    // No generic sinusoid is added to a heavy/fast falling body.
#elif GE_BIOME_PARTICLE_KIND == 3
    // Leaves tumble, then briefly glide edge-on; each has its own handedness.
    detail=vec3(sin(t*.92+phase)*cos(t*.39),.22*sin(t*1.51+phase),cos(t*.83+phase));
    turn=.62*softWave(t*.23+seed); tilt=.22+.78*abs(cos(t*1.37+phase));
#elif GE_BIOME_PARTICLE_KIND == 4
    // Petals follow a slow elliptical flutter rather than the leaf's tumbling.
    detail=vec3(sin(t*.52+phase),.28*cos(t*1.04+phase),cos(t*.52+phase)*.58);
    turn=.42*sin(t*.83+phase); tilt=.62+.38*abs(cos(t*.68+phase));
#elif GE_BIOME_PARTICLE_KIND == 5
    // Winged seeds autorotate around a descending helix.
    detail=vec3(sin(t*1.8+phase),.06*cos(t*3.6+phase),cos(t*1.8+phase))*.45;
    turn=t*2.1*(seed>.5?1.0:-1.0); tilt=.38+.62*abs(cos(t*2.1+phase));
#elif GE_BIOME_PARTICLE_KIND == 6
    // Fireflies hover in small asynchronous loops, with slow altitude changes.
    detail=vec3(sin(t*.67+phase)*cos(t*.23+phase),sin(t*.41+phase)*.55,cos(t*.53+phase));
#elif GE_BIOME_PARTICLE_KIND == 7
    // Vapour curls laterally and billows, without beating like a leaf.
    vec2 flow=eddy(position.xz*frequency,t*.21);
    detail=vec3(flow.x,.16*sin(t*.38+phase),flow.y);
    breathing=1.0+.13*sin(t*.39+phase); turn=.12*sin(t*.19+phase);
#elif GE_BIOME_PARTICLE_KIND == 8
    // Hot embers ride upward helices, unlike the hovering firefly family.
    detail=vec3(sin(t*1.3+phase),.24*sin(t*.73+phase),cos(t*1.3+phase))*.7;
    turn=.35*sin(t*1.1+phase);
#elif GE_BIOME_PARTICLE_KIND == 9
    // Heavier crystals tumble subtly; broad snow meander would look weightless.
    detail=vec3(sin(t*1.23+phase),.015*sin(t*2.46),cos(t*.91+phase))*.12;
    tilt=.65+.35*abs(cos(t*1.1+phase));
#elif GE_BIOME_PARTICLE_KIND == 11
    // Insects dart between short smooth segments; wings beat independently.
    detail=vec3(softWave(t*.63+seed),softWave(t*.47+seed*.31)*.46,softWave(t*.71+seed*.77));
    tilt=.28+.72*abs(sin(t*29.0+phase)); turn=.20*softWave(t*1.5+seed);
#elif GE_BIOME_PARTICLE_KIND == 13
    // Ash is an irregular sheet: quicker flutter and stronger edge-on tumble.
    detail=vec3(sin(t*1.61+phase)*cos(t*.51),.12*sin(t*2.37+phase),cos(t*1.09+phase));
    turn=.5*sin(t*1.7+phase); tilt=.25+.75*abs(cos(t*1.63+phase));
#elif GE_BIOME_PARTICLE_KIND == 14
    // Pollen tufts and aigrettes ride slowly changing eddies with gentle bobbing.
    vec2 flow=eddy(position.xz*frequency,t*.32);
    detail=vec3(flow.x,.28*sin(t*.63+phase),flow.y);
    turn=.35*sin(t*.57+phase);
#else
    // Spores describe loose rising spirals; emission is an independent property.
    detail=vec3(sin(t*.39+phase),.14*cos(t*.78+phase),cos(t*.39+phase))*.5;
    breathing=1.0+.08*sin(t*.71+phase); turn=.24*sin(t*.47+phase);
#endif
    position+=detail*turbulence;
    vec3 right=ubc.invView[0].xyz,up=ubc.invView[1].xyz;
    uint flags=floatBitsToUint(particle.data[sid+9u]);
#if GE_BIOME_PARTICLE_KIND == 2 || GE_BIOME_PARTICLE_KIND == 10 || GE_BIOME_PARTICLE_KIND == 12
    uint packed2=floatBitsToUint(particle.data[base+7u]);
    vec3 velocity=(vec3(packedScale>>16,packed2&65535u,packed2>>16)/32767.5-1.0)*256.0;
    vec2 axis=(mat3(ubc.view)*velocity).xy;
    axis=dot(axis,axis)>1e-8?normalize(axis):vec2(0,1);
    vec3 oldRight=right;
    right=oldRight*axis.y-up*axis.x; up=oldRight*axis.x+up*axis.y;
#else
#if GE_BIOME_PARTICLE_KIND == 7
    // Low elongated haze sheets stay horizontal; a random roll would turn the
    // same recipe into vertical curtains. Tall fumes keep their authored aspect.
    float angle=turn*.3+(seed-.5)*.08;
#else
    float angle=phase+turn;
#endif
    if((flags&(1u<<6u))!=0u)
        angle+=time*particle.data[sid+83u]*mix(.65,1.35,seed)*(seed>.5?1.0:-1.0);
    float c=cos(angle),s=sin(angle);
    vec3 oldRight=right;
    right=(oldRight*c+up*s)*tilt; up=up*c-oldRight*s;
#endif
    const vec2 corners[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    vec2 corner=corners[uint(gl_VertexID)%3u];
    float aspect=clamp(particle.data[sid+95u],.05,30.0);
    scale*=breathing;
    // Subpixel particles retain their coverage instead of flickering away.
    // Enlarge only the raster footprint, compensating opacity by its area.
    float depth=-(ubc.view*vec4(position,1)).z;
    float pixelSize=max(depth,0.0)*2.0*uEnvironmentInvResolution.y/max(abs(ubc.proj[1][1]),.001);
    float rasterScale=max(scale,pixelSize*.6);
    float footprintOpacity=(scale*scale)/(rasterScale*rasterScale);
    vec3 world=position+(right*corner.x+up*corner.y*aspect)*rasterScale;
    vec4 view=ubc.view*vec4(world,1);
    vs_out.uv=corner*.5+.5; vs_out.worldPosition=world; vs_out.viewDepth=-view.z;
    vs_out.color=readColor(base);
    float spriteRadius=scale*max(aspect,1.0);
    float nearFade=smoothstep(max(.15,spriteRadius*.6),max(.85,spriteRadius*1.6),length(position-ubc.camPos));
    float pulse=max(particle.data[sid+99u],0.0),blink=1.0;
#if GE_BIOME_PARTICLE_KIND == 6
    // Long quiet intervals and a soft flash distinguish fireflies from stars.
    float glow=max(0.0,sin(t*pulse+phase));
    blink=mix(1.0,.10+.90*glow*glow*glow*glow,clamp(pulse,0.0,1.0));
#elif GE_BIOME_PARTICLE_KIND == 8
    blink=mix(1.0,.55+.45*softWave(t*pulse*.25+seed),clamp(pulse,0.0,1.0));
#elif GE_BIOME_PARTICLE_KIND == 9
    blink=mix(1.0,.70+.30*pow(max(0.0,sin(t*pulse+phase)),8.0),clamp(pulse,0.0,1.0));
#elif GE_BIOME_PARTICLE_KIND == 15
    blink=mix(1.0,.72+.28*sin(t*pulse+phase),clamp(pulse,0.0,1.0));
#endif
    vs_out.color.a=clamp(vs_out.color.a*strength*nearFade*blink*footprintOpacity,0.0,1.0);
    vs_out.emission=max(particle.data[sid+94u],0.0); vs_out.variation=seed; vs_out.aspect=aspect;
    gl_Position=ubc.proj*view;
}
