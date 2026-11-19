#version 450 core
// Frozen unpacked texture-layout reference from 64d6db6.
// Keep the independent producer/load paths when production packing changes.
#include "../../../Asset/Shader/Atmosphere/medium.glsl"
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
// Coefficients are per metre (often below 1e-7 at altitude/night). Keep FP32
// until integration; storing them as half would erase physically thin media.
layout(rgba32f,binding=0) writeonly uniform image3D geAtmSourceOutput;
layout(rgba32f,binding=1) writeonly uniform image3D geAtmExtinctionOutput;
void main()
{
    ivec3 p=ivec3(gl_GlobalInvocationID),size=imageSize(geAtmSourceOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec3 source=vec3(0.0),extinction=vec3(0.0);
    if(p.z>0)
    {
        vec3 direction=geAtmViewDirection((vec2(p.xy)+0.5)/vec2(size.xy));
        float distance=0.5*(geAtmSliceDistance(p.z-1,size.z)+geAtmSliceDistance(p.z,size.z));
        vec3 world=geAtmCamera.xyz+direction*distance;
        source=geAtmPhysicalSource(geAtmOrigin()+direction*distance,direction,geAtmVisibility(world),extinction);
    }
    imageStore(geAtmSourceOutput,p,vec4(source,0.0));
    imageStore(geAtmExtinctionOutput,p,vec4(extinction,0.0));
}
