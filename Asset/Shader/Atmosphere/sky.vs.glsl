#version 450 core
out vec2 geSkyUV;
void main()
{
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    geSkyUV=p;
    gl_Position=vec4(p*2.0-1.0,1.0,1.0);
}
