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
vec2 objectJitter(uint h)
{ return vec2(0.05)+0.90*vec2(objectRandom(h^0x35a97fabu),objectRandom(h^0x98e02371u)); }
int objectFloorDiv(int v,int s) { int q=v/s; return q-((v<0 && v%s!=0)?1:0); }
float objectValueNoise(vec2 p,uint seed)
{
    ivec2 i=ivec2(floor(p)); vec2 f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(objectRandom(objectSiteHash(i,seed)),objectRandom(objectSiteHash(i+ivec2(1,0),seed)),f.x),
        mix(objectRandom(objectSiteHash(i+ivec2(0,1),seed)),objectRandom(objectSiteHash(i+ivec2(1),seed)),f.x),f.y);
}
#endif
