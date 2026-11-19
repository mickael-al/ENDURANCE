#version 450 core
#include "../../Asset/Shader/Atmosphere/cloud_common.glsl"
layout(local_size_x=64) in;
struct Case { vec4 position; vec4 direction; };
layout(std430,binding=0) readonly buffer Cases { Case cases[]; };
layout(std430,binding=1) writeonly buffer Results { vec4 results[]; };
uniform uint geRevisionCount;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=geRevisionCount) return;
    vec3 direction=normalize(cases[i].direction.xyz);
#ifdef GE_REVISION_REFERENCE
    float value=geAtmCloudLightRay(cases[i].position.xyz,direction,64);
    results[i]=vec4(value,value,0.0,1.0);
#else
    // This is the material path. Its linked program must not require the
    // noise sampler: texture unit 3 already belongs to material roughness.
    results[i]=vec4(geAtmCloudVisibility(cases[i].position.xyz,direction,false),
        geAtmCloudVisibility(cases[i].position.xyz,direction,true),0.0,1.0);
#endif
}
