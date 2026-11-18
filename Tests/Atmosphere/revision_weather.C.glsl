#version 450 core
#include "../../Asset/Shader/Atmosphere/cloud_common.glsl"
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Cases { vec4 positions[]; };
layout(std430,binding=1) writeonly buffer Results { vec4 results[]; };
uniform uint geRevisionCount;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=geRevisionCount) return;
    vec3 world=positions[i].xyz;
    vec2 weather=geAtmCloudWeatherAt(world,0.0);
    float height=(world.y-geAtmCloudLayer.x)/geAtmCloudLayer.y;
    results[i]=vec4(weather,geAtmCloudDensity(world),
        geAtmCloudHeightProfile(height,weather.y));
}
