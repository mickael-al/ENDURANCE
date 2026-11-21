#ifndef ENDURANCE_TREE_RUNTIME_COMMON
#define ENDURANCE_TREE_RUNTIME_COMMON
struct TreeBlock { vec4 originExtent; uvec4 info; ivec4 anchor; };
// info = first packed position, count, model, first dense transform.
// anchor = integer sector origin, world seed.
struct TreeModel
{
    vec4 centreRadius;
    vec4 scalesImpostor; // min/max scale, half extent, has impostor
    vec4 impostorCentre;
    uvec4 counts; // LOD0 wood/leaf, LOD1 wood/leaf
    uvec4 firstIndices;
    ivec4 baseVertices;
    vec4 crown; // Foliage centre, positive local tree height.
    vec4 crownShape; // Inverse foliage extents, maximum local wind displacement.
};
struct TreeInstance { vec4 positionScale; vec2 rotation; uint hash; uint model; };
layout(std430,binding=5) readonly buffer TreeBlocks { TreeBlock blocks[]; };
layout(std430,binding=6) readonly buffer TreeModels { TreeModel models[]; };
uint treeHash(uint x)
{ x^=x>>16u; x*=0x7feb352du; x^=x>>15u; x*=0x846ca68bu; return x^(x>>16u); }
float treeRandom(uint x) { return float(treeHash(x)>>8u)*(1.0/16777216.0); }
vec3 treeRotate(vec3 p,vec2 r) { return vec3(r.x*p.x+r.y*p.z,p.y,-r.y*p.x+r.x*p.z); }
vec3 treeUnrotate(vec3 p,vec2 r) { return treeRotate(p,vec2(r.x,-r.y)); }
#endif
