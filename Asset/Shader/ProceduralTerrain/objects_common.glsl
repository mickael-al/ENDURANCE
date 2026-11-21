#ifndef ENDURANCE_OBJECTS_COMMON
#define ENDURANCE_OBJECTS_COMMON
// One extra SSBO in the regular emitter (eight active blocks in total).
// Immutable tables followed by bounded, per-job hit and staging ranges.
layout(std430,binding=7) buffer ObjectWork { uint objectWords[]; };
uniform uint uObjectJobBase; // Zero disables collection for this mesh.

uint objectHash(uint x)
{ x^=x>>16u; x*=0x7feb352du; x^=x>>15u; x*=0x846ca68bu; return x^(x>>16u); }
uint objectSiteHash(ivec2 site,uint seed)
{ return objectHash(uint(site.x)^objectHash(uint(site.y)^seed)); }
float objectRandom(uint x) { return float(objectHash(x)>>8u)*(1.0/16777216.0); }
// Interleave up to six bits per coordinate, matching CPU octree intervals.
uint objectSpreadBits(uint x)
{
    x=(x|(x<<8u))&0x0000f00fu;
    x=(x|(x<<4u))&0x000c30c3u;
    return (x|(x<<2u))&0x00249249u;
}
uint objectCoverageBit(uvec3 p)
{ return objectSpreadBits(p.x)|(objectSpreadBits(p.y)<<1u)|(objectSpreadBits(p.z)<<2u); }
vec2 objectJitter(uint h)
{ return vec2(0.05)+0.90*vec2(objectRandom(h^0x35a97fabu),objectRandom(h^0x98e02371u)); }
int objectFloorDiv(int v,int s) { int q=v/s; return q-((v<0 && v%s!=0)?1:0); }
float objectValueNoise(vec2 p,uint seed)
{
    ivec2 i=ivec2(floor(p)); vec2 f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(objectRandom(objectSiteHash(i,seed)),objectRandom(objectSiteHash(i+ivec2(1,0),seed)),f.x),
        mix(objectRandom(objectSiteHash(i+ivec2(0,1),seed)),objectRandom(objectSiteHash(i+ivec2(1),seed)),f.x),f.y);
}
// Regional occupancy, independent of the candidate lattice and terrain LOD.
// Values below threshold-softness form genuinely empty clearings; the middle
// band gradually thins the forest edge rather than carpeting the whole region.
float objectForestMask(vec2 world,float period,float threshold,float softness,uint seed)
{
    float forest=0.72*objectValueNoise(world/period,seed^0x791d91u)
        +0.28*objectValueNoise(world*(3.1/period),seed^0x76c53fu);
    return smoothstep(threshold-softness,threshold+softness,forest);
}
#endif
