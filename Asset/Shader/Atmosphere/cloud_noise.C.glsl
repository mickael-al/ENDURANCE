#version 450 core
#include "atmosphere_common.glsl"
#include "../Noise/noise_3d.glsl"
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba16f,binding=0) writeonly uniform image3D geAtmNoiseOutput;

// The shared library has periodic Perlin and nonperiodic Worley. Reuse its
// hashes, wrapping and bounded jitter to make a seamless Worley texture.
float geAtmPeriodicWorley(vec3 p,uint seed,int period)
{
    ivec3 cell=ivec3(floor(p)); vec3 f=fract(p);
    float closest=4.0;
    for(int z=-1;z<=1;++z) for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
    {
        ivec3 offset=ivec3(x,y,z);
        uint h=noiseInternalHash3D(noiseInternalWrap3D(cell+offset,ivec3(period)),seed);
        uint h2=noiseInternalHash(h^0x68bc21ebu);
        vec3 jitter=0.35+0.30*vec3(h&65535u,h>>16u,h2&65535u)*(1.0/65535.0);
        vec3 d=vec3(offset)+jitter-f;
        closest=min(closest,dot(d,d));
    }
    return 1.0-clamp(sqrt(closest)/1.125833,0.0,1.0);
}

void main()
{
    ivec3 p=ivec3(gl_GlobalInvocationID),size=imageSize(geAtmNoiseOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec3 uv=(vec3(p)+0.5)/vec3(size);
    uint seed=geAtmMisc.x;
    float perlin=0.0,weight=0.0,amplitude=1.0;
    for(int octave=0;octave<4;++octave)
    {
        int period=4<<octave;
        perlin+=amplitude*noisePerlinPeriodic3D01(uv*float(period),seed^0x6a09e667u,ivec3(period));
        weight+=amplitude; amplitude*=0.5;
    }
    perlin/=weight;
    float worley=geAtmPeriodicWorley(uv*4.0,seed^0xbb67ae85u,4);
    float medium=geAtmPeriodicWorley(uv*8.0,seed^0x3c6ef372u,8);
    float detail=geAtmPeriodicWorley(uv*16.0,seed^0xa54ff53au,16);
    float shape=mix(perlin,1.0,worley*0.42);
    float fog=noisePerlinPeriodic3D01(uv*4.0,seed^0x510e527fu,ivec3(4));
    imageStore(geAtmNoiseOutput,p,vec4(shape,medium,detail,fog));
}
