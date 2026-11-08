// Seeded 3D primitives for voxel density and independently editable volume biomes.
#ifndef ENDURANCE_FIELD_NOISE_3D_GLSL
#define ENDURANCE_FIELD_NOISE_3D_GLSL

#include "field_noise_common.glsl"

uint fieldHash3(ivec3 p, uint salt)
{
    return fieldHash(uint(p.x) * 0x9e3779b9u ^ uint(p.y) * 0x85ebca6bu ^
        uint(p.z) * 0xc2b2ae35u ^ uSeed ^ salt);
}

float fieldGradient3(uint h, vec3 p)
{
    h &= 15u;
    float a = h < 8u ? p.x : p.y;
    float b = h < 4u ? p.y : (h == 12u || h == 14u ? p.x : p.z);
    return ((h & 1u) == 0u ? a : -a) + ((h & 2u) == 0u ? b : -b);
}

float fieldNoise3(vec3 p, uint salt)
{
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p), u = fieldFade(f);
    float n000 = fieldGradient3(fieldHash3(i, salt), f);
    float n100 = fieldGradient3(fieldHash3(i+ivec3(1,0,0),salt), f-vec3(1,0,0));
    float n010 = fieldGradient3(fieldHash3(i+ivec3(0,1,0),salt), f-vec3(0,1,0));
    float n110 = fieldGradient3(fieldHash3(i+ivec3(1,1,0),salt), f-vec3(1,1,0));
    float n001 = fieldGradient3(fieldHash3(i+ivec3(0,0,1),salt), f-vec3(0,0,1));
    float n101 = fieldGradient3(fieldHash3(i+ivec3(1,0,1),salt), f-vec3(1,0,1));
    float n011 = fieldGradient3(fieldHash3(i+ivec3(0,1,1),salt), f-vec3(0,1,1));
    float n111 = fieldGradient3(fieldHash3(i+ivec3(1,1,1),salt), f-vec3(1,1,1));
    return clamp(mix(mix(mix(n000,n100,u.x),mix(n010,n110,u.x),u.y),
        mix(mix(n001,n101,u.x),mix(n011,n111,u.x),u.y),u.z),-1.0,1.0);
}

// 3D Voronoi F1 distance and stable cell value. The 27-site search uses
// bounded jitter; no texture/volume storage is allocated. This helper costs
// nothing unless an independent biome function actually calls it.
vec2 fieldCellular3Data(vec3 p,uint salt)
{
    ivec3 cell=ivec3(floor(p));
    vec3 f=fract(p);
    float best=100.0;
    float site=0.0;
    for(int z=-1;z<=1;++z)
    for(int y=-1;y<=1;++y)
    for(int x=-1;x<=1;++x)
    {
        ivec3 q=ivec3(x,y,z);
        uint h=fieldHash3(cell+q,salt);
        uint h2=fieldHash(h^0x68bc21ebu);
        vec3 jitter=0.35+0.30*vec3(h&65535u,h>>16u,h2&65535u)*(1.0/65535.0);
        vec3 delta=vec3(q)+jitter-f;
        float d=dot(delta,delta);
        if(d<best)
        {
            best=d;
            site=float(h2>>8u)*(1.0/16777215.0);
        }
    }
    return vec2(sqrt(best),site);
}
float fieldCellular3(vec3 p,uint salt) { return fieldCellular3Data(p,salt).x; }

#endif
