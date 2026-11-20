#version 460 core
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;
layout(location=4) in vec4 inWind;
uniform mat4 uViewProjection;
uniform int uKind;
uniform float uGroundExtent;
uniform float uTime;
uniform float uWindStrength;
uniform vec3 uBillboardCentre;
uniform vec3 uBillboardRight;
uniform vec3 uBillboardUp;
uniform float uBillboardExtent;
out vec3 position;
out vec3 normal;
out vec2 uv;
out vec4 color;
void main()
{
    position=inPosition;
    if(uKind==3)
        position=uBillboardCentre+(uBillboardRight*inPosition.x+uBillboardUp*inPosition.y)*uBillboardExtent;
    else if(uKind==2) position.xz*=uGroundExtent;
    else if(uWindStrength>0.0)
    {
        // Shared low frequency sway preserves the silhouette across branch forks.
        float sway=sin(uTime*0.65+inPosition.y*0.09);
        position.xz+=vec2(1.0,0.35)*sway*inWind.x*inWind.x*uWindStrength;
        if(uKind==1)
            position.xz+=vec2(0.10,0.07)*sin(uTime*2.1+inWind.z*6.2831853)
                *inWind.y*uWindStrength;
    }
    normal=inNormal;
    uv=inUV;
    color=inColor;
    gl_Position=uViewProjection*vec4(position,1.0);
}
