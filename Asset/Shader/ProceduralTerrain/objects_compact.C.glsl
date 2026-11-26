#version 450 core
#include "objects_common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=3) buffer ObjectCounts { uint objectCounts[]; };
uniform uint uObjectCountBase;

// Prototype count is fixed for this catalogue and included in the binary key.
// At most 255 types / 65536 records. Scan the active chunk, not a whole sector.
shared uint typeCounts[TERRAIN_OBJECT_TYPE_COUNT];
shared uint typeOffsets[TERRAIN_OBJECT_TYPE_COUNT];

void main()
{
    uint lane=gl_LocalInvocationID.x;
    uint siteCount=objectWords[uObjectJobBase+7u],hitBase=uObjectJobBase+objectWords[15];
    for(uint type=lane;type<TERRAIN_OBJECT_TYPE_COUNT;type+=64u) typeCounts[type]=0u;
    barrier();
    for(uint site=lane;site<siteCount;site+=64u)
    {
        uint hit=objectWords[hitBase+site];
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
    for(uint site=lane;site<siteCount;site+=64u)
    {
        uint hit=objectWords[hitBase+site];
        uint type=hit&255u;
        if(hit==0u || type>=TERRAIN_OBJECT_TYPE_COUNT) continue;
        uint index=typeOffsets[type]+atomicAdd(typeCounts[type],1u);
        uint side=objectWords[3], step=objectWords[2],tileSide=objectWords[uObjectJobBase+6u];
        uint tileOrigin=objectWords[uObjectJobBase+4u];
        uvec2 local=uvec2(site%tileSide,(site/tileSide)%tileSide)+uvec2(tileOrigin&65535u,tileOrigin>>16u);
        uint band=site/(tileSide*tileSide)+(objectWords[uObjectJobBase+5u]&65535u);
        uint identity=local.x+side*local.y+side*side*band;
        ivec2 origin=ivec2(objectWords[uObjectJobBase],objectWords[uObjectJobBase+2u]);
        ivec2 worldSite=origin/int(step)+ivec2(local);
        uint h=objectSiteHash(worldSite,objectWords[uObjectJobBase+3u]);
        vec2 point=(vec2(local)+objectJitter(h))/float(side);
        uvec2 xz=uvec2(floor(point*65535.0+0.5));
        uint y=(hit>>8u)-1u;
        uint outputBase=hitBase+siteCount+2u*index;
        objectWords[outputBase]=xz.x|(y<<16u);
        objectWords[outputBase+1u]=xz.y|(identity<<16u);
    }
}
