#version 450 core
#include "field_volume.glsl"
layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) writeonly buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { FieldColumn columns[]; };
// One 48-byte SSBO column load and parameter interpolation per four Y samples.
// Shared data remains 14 floats/XZ column (1792 bytes/workgroup), as before;
// packed IDs and climate coordinates are no longer needed after preparation.
shared vec2 cachedTerrain[32];
shared vec4 cachedVolume[32];
shared vec4 cachedShape[32];
shared vec4 cachedStyle[32];
void main()
{
    uvec3 p=gl_GlobalInvocationID;
    uint side=uCells+3u;
    uint localColumn=gl_LocalInvocationID.x+gl_LocalInvocationID.z*8u;
    if(gl_LocalInvocationID.y==0u && p.x<side && p.z<side)
    {
        FieldColumn column=columns[p.x+p.z*side];
        vec2 terrain=vec2(column.terrain.x,0.0);
        vec4 volume=vec4(0.0),shape=vec4(0.0),style=vec4(0.0);
        if(uFieldMode==0u && uDetailScale>0.0 && column.terrain.w>0.0002)
        {
            // Skip table interpolation when this whole Y group misses every
            // possible band center. Use the same integer lattice and addition
            // order as evaluation, including the last partial workgroup.
            float firstY=fieldWorldPosition(ivec3(p)).y;
            float lastY=fieldWorldPosition(ivec3(p.x,min(p.y+3u,side-1u),p.z)).y;
            float minCenter=column.terrain.x+(column.blend.y-160.0*uDetailScale);
            float maxCenter=column.terrain.x+(column.blend.y+160.0*uDetailScale);
            float halfBand=0.5*column.terrain.w;
            bool below=lastY<minCenter && minCenter-lastY>halfBand;
            bool above=firstY>maxCenter && firstY-maxCenter>halfBand;
            if(!below && !above)
                fieldPrepareVolume(column,terrain,volume,shape,style);
        }
        cachedTerrain[localColumn]=terrain;
        cachedVolume[localColumn]=volume;
        cachedShape[localColumn]=shape;
        cachedStyle[localColumn]=style;
    }
    barrier();
    if(any(greaterThanEqual(p,uvec3(side)))) return;
    density[p.x+p.y*side+p.z*side*side]=fieldDensityAtPrepared(fieldWorldPosition(ivec3(p)),
        cachedTerrain[localColumn],cachedVolume[localColumn],
        cachedShape[localColumn],cachedStyle[localColumn]);
}
