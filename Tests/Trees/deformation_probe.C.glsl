#version 460 core
#define TREE_WIND 1
#define TREE_CROWN_NORMALS 0
#define TREE_SHADOW 0
#include "../../ENDURANCE/Ressources/Shader/Trees/common.glsl"
#include "../../ENDURANCE/Ressources/Shader/Trees/deformation.glsl"
layout(local_size_x=1) in;
layout(std430,binding=0) writeonly buffer Results { vec4 results[]; };
void main()
{
    TreeInstance instance; instance.positionScale=vec4(0,0,0,2);
    TreeModel model; model.crown=vec4(0,5,0,10);
    float heights[4]=float[4](0,5,10,-1);
    for(int i=0;i<4;++i)
    {
        vec3 local=vec3(0,heights[i],0),world=local*2.0,normal=vec3(1,0,0);
        treeBend(local,instance,model,vec2(4,0),world,normal);
        results[i]=vec4(world-local*2.0,0);
        if(i==1) results[4]=vec4(normal,0);
    }
    vec3 a=vec3(0,4.999,0),b=vec3(0,5.001,0),wa=a*2.0,wb=b*2.0,n=vec3(1,0,0);
    treeBend(a,instance,model,vec2(4,0),wa,n);
    treeBend(b,instance,model,vec2(4,0),wb,n);
    results[5]=vec4(normalize(wb-wa),0);
}
