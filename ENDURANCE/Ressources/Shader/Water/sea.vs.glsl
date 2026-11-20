#version 460 core
// One shared indexed 8x8 grid; each instance describes a published sea tile.
#include "sea_waves.glsl"
layout(std430,binding=0) readonly buffer UniformBufferCamera
{
    vec3 camPos;
    mat4 view;
    mat4 invView;
    mat4 proj;
} ubc;
layout(std430,binding=3) readonly buffer UniformBufferDivers
{
    int maxLight;
    float u_time;
    float gamma;
    float ambiant;
    float fov;
    bool ortho;
} ubd;
layout(std430,binding=8) readonly buffer SeaTiles { ivec4 seaTiles[]; };
uniform float seaLevel;
uniform float seaBaseExtent;
layout(location=0) out vec3 WorldPos;
layout(location=1) out vec2 SeaWaveSlope;
layout(location=2) out float SeaViewZ;
void main()
{
    ivec4 tile=seaTiles[gl_InstanceID]; // baseX, baseZ, LOD, transition mask
    uvec2 grid=uvec2(uint(gl_VertexID)%(SEA_GRID_CELLS+1u),
        uint(gl_VertexID)/(SEA_GRID_CELLS+1u));
    uint mask=uint(tile.w);
    // Collapse odd fine-edge vertices onto the preceding coarse endpoint.
    // Degenerate edge triangles leave a connecting fan: no T-junction cracks,
    // neighbour lookups, extra noise samples or per-frame mesh rebuilding.
    if((grid.x==0u && (mask&1u)!=0u) ||
       (grid.x==SEA_GRID_CELLS && (mask&2u)!=0u)) grid.y&=~1u;
    if((grid.y==0u && (mask&16u)!=0u) ||
       (grid.y==SEA_GRID_CELLS && (mask&32u)!=0u)) grid.x&=~1u;
    vec2 uv=vec2(grid)/float(SEA_GRID_CELLS);
    float lodScale=exp2(float(tile.z));
    // Same canonical lattice decode as the terrain. Both sides of a LOD seam
    // round their common endpoints identically even at large world positions.
    precise vec2 numerator=fma(vec2(tile.xy),vec2(65534.0),
        uv*65534.0*lodScale);
    precise vec2 worldXZ=numerator*(seaBaseExtent/65534.0);
    vec3 wave=seaLargeWave(worldXZ,seaLevel,ubc.camPos,ubd.u_time);
    WorldPos=vec3(worldXZ.x,seaLevel+wave.x,worldXZ.y);
    SeaWaveSlope=wave.yz;
    vec4 viewPosition=ubc.view*vec4(WorldPos,1.0);
    SeaViewZ=viewPosition.z;
    gl_Position=ubc.proj*viewPosition;
}
