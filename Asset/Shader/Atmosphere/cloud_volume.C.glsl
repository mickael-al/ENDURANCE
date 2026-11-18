#version 450 core
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba32f,binding=0) writeonly uniform image3D geAtmSourceOutput;
layout(rgba32f,binding=1) writeonly uniform image3D geAtmExtinctionOutput;
void main(){ivec3 p=ivec3(gl_GlobalInvocationID);if(any(greaterThanEqual(p,imageSize(geAtmSourceOutput))))return;imageStore(geAtmSourceOutput,p,vec4(0));imageStore(geAtmExtinctionOutput,p,vec4(0));}
