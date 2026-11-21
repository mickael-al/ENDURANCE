#version 450 core
#include "objects_common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=3) buffer ObjectCounts { uint objectCounts[]; };
uniform uint uObjectCountBase;

// Fixed for this placement catalogue/grid; the binary cache keys both defines.
// Initialization rejects more than 255 types or 1024 records per sector.
shared uint typeCounts[TERRAIN_OBJECT_TYPE_COUNT];
shared uint typeOffsets[TERRAIN_OBJECT_TYPE_COUNT];

void main()
{
    uint lane=gl_LocalInvocationID.x;
    for(uint type=lane;type<TERRAIN_OBJECT_TYPE_COUNT;type+=64u) typeCounts[type]=0u;
    barrier();
    for(uint site=lane;site<TERRAIN_OBJECT_SITE_COUNT;site+=64u)
    {
        uint hit=objectWords[uObjectJobBase+8u+site];
        uint type=hit&255u;
        if(hit!=0u && type<TERRAIN_OBJECT_TYPE_COUNT) atomicAdd(typeCounts[type],1u);
    }
    barrier();
    // <=255 small shared values. A serial exclusive scan costs no additional
    // dispatch, global prefix buffer or global atomic. One hit per site proves
    // that the sum and every resulting write fit the site-sized output region.
    if(lane==0u)
    {
        uint first=0u;
        for(uint type=0u;type<TERRAIN_OBJECT_TYPE_COUNT;++type)
        {
            uint count=typeCounts[type];
            objectCounts[uObjectCountBase+type]=count;
            typeOffsets[type]=first;
            first+=count;
            typeCounts[type]=0u;
        }
    }
    barrier();
    for(uint site=lane;site<TERRAIN_OBJECT_SITE_COUNT;site+=64u)
    {
        uint hit=objectWords[uObjectJobBase+8u+site];
        uint type=hit&255u;
        if(hit==0u || type>=TERRAIN_OBJECT_TYPE_COUNT) continue;
        uint index=typeOffsets[type]+atomicAdd(typeCounts[type],1u);
        uint side=objectWords[3], step=objectWords[2];
        uvec2 local=uvec2(site%side,(site/side)%side);
        ivec2 origin=ivec2(objectWords[uObjectJobBase],objectWords[uObjectJobBase+2u]);
        ivec2 worldSite=origin/int(step)+ivec2(local);
        uint h=objectSiteHash(worldSite,objectWords[uObjectJobBase+3u]);
        vec2 point=(vec2(local)+objectJitter(h))/float(side);
        uvec2 xz=uvec2(floor(point*65535.0+0.5));
        uint y=(hit>>8u)-1u;
        uint outputBase=uObjectJobBase+8u+TERRAIN_OBJECT_SITE_COUNT+2u*index;
        objectWords[outputBase]=xz.x|(y<<16u);
        objectWords[outputBase+1u]=xz.y|(site<<16u);
    }
}
