#version 450 core
#include "meshing_common.glsl"
#include "transition_common.glsl"

#ifndef EMIT_PASS
#define EMIT_PASS 0
#endif

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

// A workgroup covers 64 contiguous 2x2 patches on one face. Power-of-two
// chunks share a rectangular tile instead of reloading nine samples per
// patch. Emission adds a one-voxel halo in all three face axes, so even
// interpolated gradients come entirely from shared memory.
#if EMIT_PASS
shared float transitionTile[1407]; // cells 4..64: max 67 * 7 * 3 samples
#else
shared float transitionTile[325];  // cells 4..64: max 65 * 5 samples
#endif
shared uint transitionScan[64];
shared uint transitionOutputBase;

uint sampleTileIndex(uint sampleIndex, uint base, uint width)
{
    return base + sampleIndex%3u + (sampleIndex/3u)*width;
}

ivec3 samplePoint(uint sampleIndex, ivec2 patchPosition, uint face)
{
    ivec2 uv = patchPosition*2 + ivec2(int(sampleIndex%3u), int(sampleIndex/3u));
    return transitionFacePoint(face, uv, uCells);
}

float sampleDensity(uint sampleIndex, ivec2 patchPosition, uint face,
                    bool tiled, uint base, uint width)
{
    return tiled ? transitionTile[sampleTileIndex(sampleIndex, base, width)]
                 : densityAt(samplePoint(sampleIndex, patchPosition, face));
}

#if EMIT_PASS
vec3 sampleGradient(uint sampleIndex, ivec2 patchPosition, uint face,
                    bool tiled, uint base, uint width, uint plane)
{
    if (!tiled) return gradientAt(samplePoint(sampleIndex, patchPosition, face));
    uint i = sampleTileIndex(sampleIndex, base, width);
    vec3 gradient = vec3(transitionTile[i+1u]-transitionTile[i-1u],
                         transitionTile[i+width]-transitionTile[i-width],
                         transitionTile[i+plane]-transitionTile[i-plane]);
    return transitionFaceVector(face, gradient);
}

uvec3 coarsePositionBits(ivec3 a, ivec3 b, float t, uint face)
{
    // Interpolate in the neighbor's own coordinate system, exactly as its
    // regular-cell shader does. Quantizing the fine position first is not
    // equivalent: adding a half-chunk after interpolation can lose a float
    // mantissa bit and change which side of an integer rounding boundary wins.
    vec3 coarseOffset = vec3(uChunkParity)*(0.5*float(uCells));
    ivec3 packedOffset = ivec3(uChunkParity)*int(POSITION_MAX);
    uint axis = face/2u;
    if ((face & 1u) == 0u)
    {
        coarseOffset[axis] = float(uCells);
        packedOffset[axis] = 2*int(POSITION_MAX);
    }
    else
    {
        coarseOffset[axis] = -0.5*float(uCells);
        packedOffset[axis] = -int(POSITION_MAX);
    }
    vec3 coarseA = vec3(a)*0.5+coarseOffset;
    vec3 coarseB = vec3(b)*0.5+coarseOffset;
    // All official edge endpoint pairs ascend along their world-space axis,
    // for both regular corners and the six face coordinate transforms.
    vec3 coarsePosition = interpolateEdge(coarseA, coarseB, t);
    vec3 q = clamp(coarsePosition/float(uCells), 0.0, 1.0)*POSITION_MAX;
    ivec3 coarseBits = ivec3(floor(q+vec3(0.5)));
    return uvec3(clamp(2*coarseBits-packedOffset, ivec3(0), ivec3(int(POSITION_MAX))));
}

#if TERRAIN_EDGE_REUSE
uint transitionEdgeAt(uvec4 first,uvec4 second,uint last,uint i)
{
    uint word=i<16u?first[i/4u]:(i<32u?second[i/4u-4u]:last);
    return (word>>((i%4u)*8u))&255u;
}

uint transitionEdgeKey(uint a,uint b)
{
    // Fine grid: six horizontal and six vertical edges. Coarse duplicates
    // have their own four identities even where the source samples coincide.
    if(a<9u) return b-a==1u?2u*(a/3u)+a%3u:6u+a;
    a-=9u; b-=9u;
    return b-a==1u?12u+a/2u:14u+(a&1u);
}
#endif
#endif

void main()
{
    uint face = gl_WorkGroupID.y;
    // Uniform for the whole workgroup: safe before any barrier.
    if (face >= 6u) return;
    uint group=transitionGroupIndex();
    if (skipMeshGroup(group,(uTransitionMask & (1u << face)) != 0u)) return;

    uint lane = gl_LocalInvocationIndex;
    uint patchSide = uCells/2u;
    uint firstPatch = gl_WorkGroupID.x*64u;
    uint patchIndex = firstPatch+lane;
    ivec2 patchPosition = ivec2(int(patchIndex%patchSide), int(patchIndex/patchSide));
    bool patchActive = patchIndex < patchSide*patchSide;
    bool tiled = (patchSide & (patchSide-1u)) == 0u;

    uint tileColumns = min(patchSide, 64u);
    // Small 4/8-cell chunks contain fewer than 64 patches in the whole face.
    // Their inactive lanes need no rows outside the actual patch rectangle.
    uint tileRows = min(64u/tileColumns,patchSide);
#if EMIT_PASS
    const uint border = 1u;
    const uint planes = 3u;
#else
    const uint border = 0u;
    const uint planes = 1u;
#endif
    uint width = 2u*tileColumns+1u+2u*border;
    uint height = 2u*tileRows+1u+2u*border;
    uint plane = width*height;
    ivec2 tileOrigin = 2*ivec2(int(firstPatch%patchSide), int(firstPatch/patchSide));
    if (tiled)
    {
        for (uint i = lane; i < plane*planes; i += 64u)
        {
            ivec3 p = ivec3(tileOrigin + ivec2(int(i%width), int((i/width)%height)) - ivec2(border),
                             int(i/plane)-int(border));
            transitionTile[i] = densityAt(transitionFacePoint(face, p, uCells));
        }
    }
    barrier();
    uint base = (lane%tileColumns)*2u+border
              + ((lane/tileColumns)*2u+border)*width+border*plane;

    uint caseIndex = 0u;
    if (patchActive)
    {
        for (uint i = 0u; i < 9u; ++i)
            if (sampleDensity(i, patchPosition, face, tiled, base, width) > 0.0)
                caseIndex |= 1u << transitionCaseBit(i);
    }
    uint tableBase = TRANSITION_TABLE_OFFSET + caseIndex*TRANSITION_TABLE_STRIDE;
    uint count = patchActive ? tables[tableBase] : 0u;
    transitionScan[lane] = count;
    barrier();

#if EMIT_PASS
    for (uint stride = 1u; stride < 64u; stride <<= 1u)
    {
        uint addend = lane >= stride ? transitionScan[lane-stride] : 0u;
        barrier();
        transitionScan[lane] += addend;
        barrier();
    }
    if (lane == 0u) transitionOutputBase = transitionScan[63]>0u ? atomicAdd(cursor, transitionScan[63]) : 0u;
    barrier();
    uint first = transitionOutputBase+transitionScan[lane]-count;
#if TERRAIN_EDGE_REUSE
    uvec4 edges0=uvec4(count>0u?tables[tableBase+1u]:0u,
                      count>4u?tables[tableBase+2u]:0u,
                      count>8u?tables[tableBase+3u]:0u,
                      count>12u?tables[tableBase+4u]:0u);
    uvec4 edges1=uvec4(count>16u?tables[tableBase+5u]:0u,
                      count>20u?tables[tableBase+6u]:0u,
                      count>24u?tables[tableBase+7u]:0u,
                      count>28u?tables[tableBase+8u]:0u);
    uint edges2=count>32u?tables[tableBase+9u]:0u;
    uint seen=0u;
#endif
    for (uint i = 0u; i < count; ++i)
    {
#if TERRAIN_EDGE_REUSE
        uint edge=transitionEdgeAt(edges0,edges1,edges2,i);
#else
        uint edge = (tables[tableBase+1u+i/4u] >> ((i%4u)*8u)) & 255u;
#endif
        uint endpointA = edge >> 4u;
        uint endpointB = edge & 15u;
#if TERRAIN_EDGE_REUSE
        uint bit=1u<<transitionEdgeKey(endpointA,endpointB);
        if((seen&bit)!=0u) continue;
        seen|=bit;
#endif
        uint a = transitionSampleIndex(endpointA);
        uint b = transitionSampleIndex(endpointB);
        float t = edgeFraction(sampleDensity(a, patchPosition, face, tiled, base, width),
                               sampleDensity(b, patchPosition, face, tiled, base, width));
        vec3 p = interpolateEdge(vec3(samplePoint(a, patchPosition, face)),
                                 vec3(samplePoint(b, patchPosition, face)), t);
        vec3 normal = normalFromGradients(sampleGradient(a, patchPosition, face, tiled, base, width, plane),
                                         sampleGradient(b, patchPosition, face, tiled, base, width, plane), t);
        vec2 climate = climateAt(p);
        bool coarse = endpointA >= 9u && endpointB >= 9u;
        uvec3 encoded;
        if (coarse)
        {
            uvec3 bits = coarsePositionBits(samplePoint(a, patchPosition, face),
                                           samplePoint(b, patchPosition, face), t, face);
            encoded=encodePackedVertex(bits,normal,climate);
        }
        else
        {
            p = transitionAdjustedPosition(p, normal, uTransitionMask, uCells);
            encoded=encodeVertex(p,normal,climate,false);
        }
        emitEncodedVertex(first+i,encoded);
#if TERRAIN_EDGE_REUSE
        for(uint j=i+1u;j<count;++j)
            if(transitionEdgeAt(edges0,edges1,edges2,j)==edge)
                emitEncodedVertex(first+j,encoded);
#endif
    }
#else
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (lane < stride) transitionScan[lane] += transitionScan[lane+stride];
        barrier();
    }
    if (lane == 0u)
    {
        recordMeshGroup(group,transitionScan[0]);
        if(transitionScan[0]!=0u) atomicAdd(total,transitionScan[0]);
    }
#endif
}
