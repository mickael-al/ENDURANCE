#version 450 core
layout(local_size_x=1) in;
layout(std140,binding=6) uniform AtmosphereParameters { vec4 parameters[42]; };
layout(std430,binding=7) buffer Results { vec4 values[]; };
uniform uint resultIndex;
void main() { values[resultIndex]=parameters[15]; } // camera position and clock
