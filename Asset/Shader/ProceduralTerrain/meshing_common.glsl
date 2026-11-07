#ifndef PROCEDURAL_TERRAIN_MESHING_COMMON
#define PROCEDURAL_TERRAIN_MESHING_COMMON

// Match the 64-byte production FieldColumn, including preblended 3D recipes.
struct MeshColumn { vec4 terrain; vec4 volume; vec4 shape; vec4 style; };
layout(std430,binding=0) readonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { MeshColumn columns[]; };
layout(std430,binding=2) readonly buffer TransvoxelTables { uint tables[]; };
layout(std430,binding=3) buffer MeshCounters
{
    uint total;
    uint cursor;
    uint overflow;
    uint padding;
};
layout(std430,binding=4) writeonly buffer MeshVertices { uint vertices[]; };

uniform uint uCells;
uniform uint uTransitionMask;
uniform uvec3 uChunkParity;
uniform uint uVertexBase;
uniform uint uVertexCapacity;
uniform uint uChunkSlot;
uniform uint uVertexFirst;

const uint REGULAR_TABLE_STRIDE = 5u;
const uint TRANSITION_TABLE_OFFSET = 1280u;
const uint TRANSITION_TABLE_STRIDE = 10u;
const float POSITION_MAX = 65534.0;

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
    return mix(mix(columns[a.x+a.y*side].terrain.yz,columns[b.x+a.y*side].terrain.yz,f.x),
               mix(columns[a.x+b.y*side].terrain.yz,columns[b.x+b.y*side].terrain.yz,f.x),f.y);
}

uint packNormal(vec3 n)
{
    n/=max(abs(n.x)+abs(n.y)+abs(n.z),1e-20);
    vec2 oct=n.xy;
    if(n.z<0.0) oct=(1.0-abs(oct.yx))*vec2(oct.x>=0.0?1.0:-1.0,oct.y>=0.0?1.0:-1.0);
    return packSnorm2x16(oct);
}

void emitPackedVertex(uint destination,uvec3 positionBits,vec3 normal,vec2 climate)
{
    if(destination>=uVertexCapacity)
    {
        atomicOr(overflow,1u);
        return;
    }
    uvec2 biome=uvec2(round(clamp(climate,0.0,1.0)*255.0));
    uint target=(uVertexBase+destination)*3u;
    vertices[target]=positionBits.x|(positionBits.y<<16u);
    vertices[target+1u]=positionBits.z|(biome.x<<16u)|(biome.y<<24u);
    vertices[target+2u]=packNormal(normal);
}

void emitVertex(uint destination,vec3 p,vec3 normal,vec2 climate,bool coarseSnap)
{
    // An even denominator makes the half-chunk offset exact. Transition
    // coarse vertices are quantized in the neighbor's coordinate frame by
    // transition.C.glsl and passed directly to emitPackedVertex.
    vec3 q=clamp(p/float(uCells),0.0,1.0)*POSITION_MAX;
    // Explicit half-up rounding commutes with integer translations. GLSL
    // round() may choose even ties after the half-chunk offset of 32767 codes.
    q=coarseSnap ? 2.0*floor(q*0.5+vec3(0.5)) : floor(q+vec3(0.5));
    emitPackedVertex(destination,uvec3(clamp(q,0.0,POSITION_MAX)),normal,climate);
}

#endif
