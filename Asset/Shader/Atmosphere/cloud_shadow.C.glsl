#version 450 core
layout(local_size_x=8,local_size_y=8) in;
layout(rg16f,binding=0) writeonly uniform image2D geAtmShadowOutput;
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(geAtmShadowOutput))))return;imageStore(geAtmShadowOutput,p,vec4(1));}
