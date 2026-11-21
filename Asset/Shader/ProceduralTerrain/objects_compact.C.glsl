#version 450 core
#include "objects_common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=3) buffer ObjectCounts { uint objectCounts[]; };
uniform uint uObjectCountBase;
void main()
{
    uint site=gl_GlobalInvocationID.x, count=objectWords[7];
    if(site>=count) return;
    uint hit=objectWords[uObjectJobBase+8u+site];
    if(hit==0u) return;
    uint type=hit&255u;
    if(type>=objectWords[0]) return;
    uint index=atomicAdd(objectCounts[uObjectCountBase+type],1u);
    if(index>=count) return; // Also bounded algebraically: <=1 hit per record.
    uint side=objectWords[3], step=objectWords[2];
    uvec2 local=uvec2(site%side,(site/side)%side);
    ivec2 origin=ivec2(objectWords[uObjectJobBase],objectWords[uObjectJobBase+2u]);
    ivec2 worldSite=origin/int(step)+ivec2(local);
    uint h=objectSiteHash(worldSite,objectWords[uObjectJobBase+3u]);
    vec2 point=(vec2(local)+objectJitter(h))/float(side);
    uvec2 xz=uvec2(floor(point*65535.0+0.5));
    uint y=(hit>>8u)-1u;
    uint outputBase=uObjectJobBase+8u+count+2u*(type*count+index);
    objectWords[outputBase]=xz.x|(y<<16u);
    objectWords[outputBase+1u]=xz.y|(site<<16u);
}
