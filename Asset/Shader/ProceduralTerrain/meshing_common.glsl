#ifndef PROCEDURAL_TERRAIN_MESHING_COMMON
#define PROCEDURAL_TERRAIN_MESHING_COMMON

#include "field_storage.glsl"

#ifndef EMIT_PASS
#define EMIT_PASS 0
#endif
#ifndef TERRAIN_GROUP_COUNTS
#define TERRAIN_GROUP_COUNTS 0
#endif
#ifndef TERRAIN_FIELD_STATE
#define TERRAIN_FIELD_STATE 0
#endif
#ifndef TERRAIN_EDGE_REUSE
#define TERRAIN_EDGE_REUSE 1
#endif

// The extraction passes only consume climate from the finalized 32-byte view.
layout(std430,binding=0) readonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { PackedFieldColumn columns[]; };
layout(std430,binding=2) readonly buffer TransvoxelTables { uint tables[]; };
layout(std430,binding=3) buffer MeshCounters
{
    uint total;
    uint cursor;
    uint overflow;
    uint padding;
};
layout(std430,binding=4) writeonly buffer MeshVertices { uint vertices[]; };
#if TERRAIN_GROUP_COUNTS
layout(std430,binding=5) buffer MeshGroupCounts { uint meshGroupCounts[]; };
#endif
#if TERRAIN_FIELD_STATE
// The complete state arena is bound: a 16-byte record is not necessarily a
// legal SSBO range offset on drivers requiring 256-byte alignment.
layout(std430,binding=6) readonly buffer MeshFieldState { uvec4 meshFieldStates[]; };
uniform uint uFieldStateIndex;
#endif

uniform uint uCells;
uniform uint uTransitionMask;
uniform uvec3 uChunkParity;
uniform uint uVertexBase;
uniform uint uVertexCapacity;

const uint REGULAR_TABLE_STRIDE = 5u;
const uint TRANSITION_TABLE_OFFSET = 1280u;
const uint TRANSITION_TABLE_STRIDE = 10u;
const float POSITION_MAX = 65534.0;

uint regularGroupCount()
{
    return ((uCells+7u)/8u)*((uCells+3u)/4u)*((uCells+3u)/4u);
}

uint regularGroupIndex()
{
    return gl_WorkGroupID.x+((uCells+7u)/8u)*
        (gl_WorkGroupID.y+((uCells+3u)/4u)*gl_WorkGroupID.z);
}

uint transitionGroupIndex()
{
    uint patches=(uCells/2u)*(uCells/2u);
    return regularGroupCount()+gl_WorkGroupID.y*((patches+63u)/64u)+gl_WorkGroupID.x;
}

// Every condition here is identical for the entire workgroup. Empty groups
// leave before their cooperative loads and barriers. Count also clears unused
// face records, so changing a transition mask cannot resurrect stale counts.
bool skipMeshGroup(uint group, bool enabled)
{
#if TERRAIN_FIELD_STATE
    enabled=enabled && meshFieldStates[uFieldStateIndex].x==3u;
#endif
    if(!enabled)
    {
#if TERRAIN_GROUP_COUNTS && !EMIT_PASS
        if(gl_LocalInvocationIndex==0u) meshGroupCounts[group]=0u;
#endif
        return true;
    }
#if TERRAIN_GROUP_COUNTS && EMIT_PASS
    return meshGroupCounts[group]==0u;
#else
    return false;
#endif
}

void recordMeshGroup(uint group, uint count)
{
#if TERRAIN_GROUP_COUNTS
    meshGroupCounts[group]=count;
#endif
}

float densityAt(ivec3 p)
{
    uint side=uCells+3u;
    uvec3 q=uvec3(clamp(p+ivec3(1),ivec3(0),ivec3(side-1u)));
    return density[q.x+q.y*side+q.z*side*side];
}

vec3 gradientAt(ivec3 p)
{
    return vec3(densityAt(p+ivec3(1,0,0))-densityAt(p-ivec3(1,0,0)),
                densityAt(p+ivec3(0,1,0))-densityAt(p-ivec3(0,1,0)),
                densityAt(p+ivec3(0,0,1))-densityAt(p-ivec3(0,0,1)));
}

float edgeFraction(float a,float b)
{
    float divisor=a-b;
    return abs(divisor)>1e-30 ? clamp(a/divisor,0.0,1.0) : 0.5;
}

vec3 interpolateEdge(vec3 a,vec3 b,float t)
{
    // Shared arithmetic for both independently compiled extraction programs.
    // Prevent a driver from contracting only one side of a LOD interface.
    precise vec3 position=a+(b-a)*t;
    return position;
}

vec3 normalFromGradients(vec3 a,vec3 b,float t)
{
    precise vec3 n=-(a+(b-a)*t);
    precise float length2=dot(n,n);
    precise vec3 normal=length2>1e-20 ? n*inversesqrt(length2) : vec3(0,1,0);
    return normal;
}

vec3 edgeNormal(ivec3 a,ivec3 b,float t)
{
    return normalFromGradients(gradientAt(a),gradientAt(b),t);
}

vec2 climateAt(vec3 p)
{
    uint side=uCells+3u;
    vec2 q=clamp(p.xz+vec2(1),vec2(0),vec2(side-1u));
    uvec2 a=uvec2(floor(q));
    uvec2 b=min(a+uvec2(1),uvec2(side-1u));
    vec2 f=fract(q);
    return mix(mix(fieldStoredClimate(columns[a.x+a.y*side]),fieldStoredClimate(columns[b.x+a.y*side]),f.x),
               mix(fieldStoredClimate(columns[a.x+b.y*side]),fieldStoredClimate(columns[b.x+b.y*side]),f.x),f.y);
}

// A regular edge varies along exactly one lattice axis. Keep the same p+1,
// floor and interpolation arithmetic as climateAt, but avoid unused corners.
// Transition coarse edges span two fine cells and keep the bilinear routine.
vec2 regularEdgeClimate(vec3 p,uint direction)
{
    uint side=uCells+3u;
    vec2 q=clamp(p.xz+vec2(1),vec2(0),vec2(side-1u));
    uvec2 a=uvec2(floor(q));
    vec2 first=fieldStoredClimate(columns[a.x+a.y*side]);
    if(direction==2u) return first;
    uvec2 b=min(a+uvec2(direction==1u?1u:0u,direction==4u?1u:0u),uvec2(side-1u));
    vec2 second=fieldStoredClimate(columns[b.x+b.y*side]);
    return mix(first,second,direction==1u?fract(q.x):fract(q.y));
}

uint packNormal(vec3 n)
{
    n/=max(abs(n.x)+abs(n.y)+abs(n.z),1e-20);
    vec2 oct=n.xy;
    if(n.z<0.0) oct=(1.0-abs(oct.yx))*vec2(oct.x>=0.0?1.0:-1.0,oct.y>=0.0?1.0:-1.0);
    return packSnorm2x16(oct);
}

uvec3 encodePackedVertex(uvec3 positionBits,vec3 normal,vec2 climate)
{
    uvec2 biome=uvec2(round(clamp(climate,0.0,1.0)*255.0));
    return uvec3(positionBits.x|(positionBits.y<<16u),
        positionBits.z|(biome.x<<16u)|(biome.y<<24u),packNormal(normal));
}

void emitEncodedVertex(uint destination,uvec3 encoded)
{
    if(destination>=uVertexCapacity)
    {
        atomicOr(overflow,1u);
        return;
    }
    uint target=(uVertexBase+destination)*3u;
    vertices[target]=encoded.x;
    vertices[target+1u]=encoded.y;
    vertices[target+2u]=encoded.z;
}

uvec3 encodeVertex(vec3 p,vec3 normal,vec2 climate,bool coarseSnap)
{
    // An even denominator makes the half-chunk offset exact. Transition
    // coarse vertices are quantized in the neighbor's coordinate frame by
    // transition.C.glsl and passed directly to emitPackedVertex.
    vec3 q=clamp(p/float(uCells),0.0,1.0)*POSITION_MAX;
    // Explicit half-up rounding commutes with integer translations. GLSL
    // round() may choose even ties after the half-chunk offset of 32767 codes.
    q=coarseSnap ? 2.0*floor(q*0.5+vec3(0.5)) : floor(q+vec3(0.5));
    return encodePackedVertex(uvec3(clamp(q,0.0,POSITION_MAX)),normal,climate);
}

void emitPackedVertex(uint destination,uvec3 positionBits,vec3 normal,vec2 climate)
{
    emitEncodedVertex(destination,encodePackedVertex(positionBits,normal,climate));
}

void emitVertex(uint destination,vec3 p,vec3 normal,vec2 climate,bool coarseSnap)
{
    emitEncodedVertex(destination,encodeVertex(p,normal,climate,coarseSnap));
}

#endif
