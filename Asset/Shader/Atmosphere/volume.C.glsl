#version 450 core
#include "atmosphere_common.glsl"
layout(local_size_x=8,local_size_y=8) in;
layout(rgba16f,binding=0) writeonly uniform image3D geAtmScatteringOutput;
layout(rgba16f,binding=1) writeonly uniform image3D geAtmTransmissionOutput;
layout(rgba16f,binding=2) writeonly uniform image2D geAtmBaselineScatteringOutput;
layout(rgba16f,binding=3) writeonly uniform image2D geAtmBaselineTransmissionOutput;
layout(binding=9) uniform sampler3D geAtmPhysicalSourceVolume;
layout(binding=10) uniform sampler3D geAtmPhysicalExtinctionVolume;
void main()
{
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);ivec3 size=imageSize(geAtmScatteringOutput);
 if(any(greaterThanEqual(p,size.xy)))return;
 vec3 s=vec3(0),t=vec3(1);
 imageStore(geAtmScatteringOutput,ivec3(p,0),vec4(0));imageStore(geAtmTransmissionOutput,ivec3(p,0),vec4(1));
 for(int z=1;z<size.z;++z)
 {
  geAtmIntegrate(s,t,texelFetch(geAtmPhysicalSourceVolume,ivec3(p,z),0).rgb,texelFetch(geAtmPhysicalExtinctionVolume,ivec3(p,z),0).rgb,geAtmSliceDistance(z,size.z)-geAtmSliceDistance(z-1,size.z));
  imageStore(geAtmScatteringOutput,ivec3(p,z),vec4(s,1));imageStore(geAtmTransmissionOutput,ivec3(p,z),vec4(t,1));
 }
 imageStore(geAtmBaselineScatteringOutput,p,vec4(s,1));imageStore(geAtmBaselineTransmissionOutput,p,vec4(t,1));
}
