#version 450 core
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba16f,binding=0) writeonly uniform image3D geAtmNoiseOutput;
void main(){ivec3 p=ivec3(gl_GlobalInvocationID);if(any(greaterThanEqual(p,imageSize(geAtmNoiseOutput))))return;imageStore(geAtmNoiseOutput,p,vec4(0));}
