#version 450 core

// Rain uses the existing particle simulation/ABI, but a cylindrical world-up
// card instead of the generic camera-plane or velocity-oriented billboard.
layout(std430,binding=0) readonly buffer UniformBufferCamera
{ vec3 camPos; mat4 view; mat4 invView; mat4 proj; } ubc;
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
    uint a=floatBitsToUint(particle.data[base+3u]);
    uint b=floatBitsToUint(particle.data[base+4u]);
    return vec4(a&65535u,a>>16,b&65535u,b>>16)*(64.0/65535.0);
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
    uint packedScale=floatBitsToUint(particle.data[base+6u]);
    float scale=float(packedScale&65535u)*(64.0/65535.0);
    float strength=particle.data[sid+93u];
    if(scale<=0.00001 || strength<=0.0 || particle.data[base+5u]<=0.0) return;

    // Only simulation updates move the center. Moving the emitter or turning
    // the camera must not drag a living drop or add unrelated sprite flutter.
    vec3 position=vec3(particle.data[base],particle.data[base+1u],particle.data[base+2u])
        +vec3(particle.data[sid+31u],particle.data[sid+32u],particle.data[sid+33u]);
    const vec3 up=vec3(0,1,0);
    vec2 towardCamera=(ubc.camPos-position).xz;
    float horizontalLength2=dot(towardCamera,towardCamera);
    vec3 right;
    if(horizontalLength2>1e-10)
        right=vec3(towardCamera.y,0,-towardCamera.x)*inversesqrt(horizontalLength2);
    else
    {
        // Directly above/below a drop, a vertical card naturally goes edge-on.
        // Keep a finite horizontal width axis; never tilt the long axis into
        // the camera plane to hide that physical foreshortening.
        vec2 cameraRight=ubc.invView[0].xz;
        float length2=dot(cameraRight,cameraRight);
        right=length2>1e-10 ? vec3(cameraRight.x,0,cameraRight.y)*inversesqrt(length2) : vec3(1,0,0);
    }

    const vec2 corners[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    vec2 corner=corners[uint(gl_VertexID)%3u];
    float aspect=clamp(particle.data[sid+95u],.05,30.0);
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
    vs_out.color.a=clamp(vs_out.color.a*strength*nearFade*footprintOpacity,0.0,1.0);
    vs_out.emission=max(particle.data[sid+94u],0.0);
    vs_out.variation=random01(id+sid*1664525u); vs_out.aspect=aspect;
    // The shared fragment shader clips this oversized triangle to UV [0,1]^2,
    // shapes the rain streak, and retains opaque-depth/atmosphere composition.
    gl_Position=ubc.proj*view;
}
