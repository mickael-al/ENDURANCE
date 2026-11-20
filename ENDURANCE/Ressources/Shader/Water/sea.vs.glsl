#version 460 core
// One shared six-vertex quad; each instance describes one published sea tile.
layout(std430,binding=0) readonly buffer UniformBufferCamera
{
    vec3 camPos;
    mat4 view;
    mat4 invView;
    mat4 proj;
} ubc;
layout(std430,binding=8) readonly buffer SeaTiles { ivec4 seaTiles[]; };
uniform float seaLevel;
uniform float seaBaseExtent;
layout(location=0) out vec3 WorldPos;
void main()
{
    const vec2 corners[6]=vec2[6](vec2(0,0),vec2(0,1),vec2(1,0),
                                vec2(1,0),vec2(0,1),vec2(1,1));
    ivec4 tile=seaTiles[gl_InstanceID]; // baseX, baseZ, LOD, reserved
    float lodScale=exp2(float(tile.z));
    // Same canonical lattice decode as the terrain. Both sides of a LOD seam
    // round their common endpoints identically even at large world positions.
    precise vec2 numerator=fma(vec2(tile.xy),vec2(65534.0),
        corners[gl_VertexID]*65534.0*lodScale);
    precise vec2 worldXZ=numerator*(seaBaseExtent/65534.0);
    WorldPos=vec3(worldXZ.x,seaLevel,worldXZ.y);
    gl_Position=ubc.proj*ubc.view*vec4(WorldPos,1.0);
}
