#version 450 core
#include "meshing_common.glsl"
#include "transition_common.glsl"

#ifndef EMIT_PASS
#define EMIT_PASS 0
#endif

layout(local_size_x=8,local_size_y=4,local_size_z=4) in;

// Count needs 225 samples for 128 cells. Emission adds a one-voxel halo:
// 539 cooperative loads also supply every central-difference normal, instead
// of twelve global density reads for each emitted triangle vertex.
#if EMIT_PASS
const uint TILE_WIDTH=11u;
const uint TILE_HEIGHT=7u;
const uint TILE_PLANE=77u;
const uint TILE_SAMPLES=539u;
const int TILE_BORDER=1;
shared float tile[539];
#else
const uint TILE_WIDTH=9u;
const uint TILE_HEIGHT=5u;
const uint TILE_PLANE=45u;
const uint TILE_SAMPLES=225u;
const int TILE_BORDER=0;
shared float tile[225];
#endif
shared uint scan[128];
shared uint outputBase;

ivec3 corner(uint i)
{
    return ivec3(int(i&1u),int((i>>1u)&1u),int((i>>2u)&1u));
}

uint cornerOffset(uint i)
{
    return (i&1u)+((i>>1u)&1u)*TILE_WIDTH+((i>>2u)&1u)*TILE_PLANE;
}

#if EMIT_PASS
vec3 tileGradient(uint i)
{
    return vec3(tile[i+1u]-tile[i-1u],
                tile[i+TILE_WIDTH]-tile[i-TILE_WIDTH],
                tile[i+TILE_PLANE]-tile[i-TILE_PLANE]);
}
#endif

void main()
{
    uint lane=gl_LocalInvocationIndex;
    uint group=regularGroupIndex();
    if(skipMeshGroup(group,true)) return;
    ivec3 groupOrigin=ivec3(gl_WorkGroupID)*ivec3(8,4,4);
    for(uint i=lane;i<TILE_SAMPLES;i+=128u)
    {
        ivec3 q=ivec3(int(i%TILE_WIDTH),int((i/TILE_WIDTH)%TILE_HEIGHT),int(i/TILE_PLANE))-ivec3(TILE_BORDER);
        tile[i]=densityAt(groupOrigin+q);
    }
    barrier();

    ivec3 cell=ivec3(gl_GlobalInvocationID);
    bool cellActive=all(lessThan(cell,ivec3(uCells)));
    uint caseIndex=0u;
    float values[8];
    uvec3 local=gl_LocalInvocationID+uvec3(TILE_BORDER);
    uint localBase=local.x+local.y*TILE_WIDTH+local.z*TILE_PLANE;
    for(uint c=0u;c<8u;++c)
    {
        values[c]=tile[localBase+cornerOffset(c)];
        if(values[c]>0.0) caseIndex|=1u<<c;
    }
    uint count=cellActive?tables[caseIndex*REGULAR_TABLE_STRIDE]:0u;
    scan[lane]=count;
    barrier();
#if EMIT_PASS
    // Inclusive workgroup scan. Only one global allocation per workgroup.
    for(uint stride=1u;stride<128u;stride<<=1u)
    {
        uint addend=lane>=stride?scan[lane-stride]:0u;
        barrier();
        scan[lane]+=addend;
        barrier();
    }
    if(lane==0u) outputBase=scan[127]>0u?atomicAdd(cursor,scan[127]):0u;
    barrier();
    uint first=outputBase+scan[lane]-count;
    // Every output is owned by this lane; the three uint words never require
    // atomic bitfield updates and cannot race with a neighbouring vertex.
#if TERRAIN_EDGE_REUSE
    // Only table words are cached, rather than twelve positions/normals or a
    // dynamically indexed vertex array that can spill to per-thread memory.
    uint tableBase=caseIndex*REGULAR_TABLE_STRIDE;
    uvec4 edgeWords=uvec4(count>0u?tables[tableBase+1u]:0u,
                         count>4u?tables[tableBase+2u]:0u,
                         count>8u?tables[tableBase+3u]:0u,
                         count>12u?tables[tableBase+4u]:0u);
    uint seen=0u;
#endif
    for(uint i=0u;i<count;++i)
    {
#if TERRAIN_EDGE_REUSE
        uint edge=(edgeWords[i/4u]>>((i%4u)*8u))&255u;
#else
        uint word=tables[caseIndex*REGULAR_TABLE_STRIDE+1u+i/4u];
        uint edge=(word>>((i%4u)*8u))&255u;
#endif
        uint ia=(edge>>4u)&15u;
        uint ib=edge&15u;
#if TERRAIN_EDGE_REUSE
        uint direction=ia^ib;
        uint key=direction==1u?ia/2u:(direction==2u?4u+(ia&1u)+((ia>>1u)&2u):8u+(ia&3u));
        uint bit=1u<<key;
        if((seen&bit)!=0u) continue;
        seen|=bit;
#endif
        ivec3 a=cell+corner(ia);
        ivec3 b=cell+corner(ib);
        float t=edgeFraction(values[ia],values[ib]);
        vec3 p=interpolateEdge(vec3(a),vec3(b),t);
        vec3 normal=normalFromGradients(tileGradient(localBase+cornerOffset(ia)),tileGradient(localBase+cornerOffset(ib)),t);
        vec2 climate=regularEdgeClimate(p,ia^ib);
        p=transitionAdjustedPosition(p,normal,uTransitionMask,uCells);
#if TERRAIN_EDGE_REUSE
        uvec3 encoded=encodeVertex(p,normal,climate,false);
        emitEncodedVertex(first+i,encoded);
        // Repeated occurrences keep their original triangle order. The only
        // additional work is a short scan of register-resident table bytes.
        for(uint j=i+1u;j<count;++j)
            if(((edgeWords[j/4u]>>((j%4u)*8u))&255u)==edge)
                emitEncodedVertex(first+j,encoded);
#else
        emitVertex(first+i,p,normal,climate,false);
#endif
    }
#else
    for(uint stride=64u;stride>0u;stride>>=1u)
    {
        if(lane<stride) scan[lane]+=scan[lane+stride];
        barrier();
    }
    if(lane==0u)
    {
        recordMeshGroup(group,scan[0]);
        if(scan[0]>0u) atomicAdd(total,scan[0]);
    }
#endif
}
