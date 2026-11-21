#version 460 core
#include "common.glsl"
#if !TREE_SHADOW
#include "../../../../Asset/Shader/Geometry/gbuffer_position.glsl"
#include "../../../../Asset/Shader/Geometry/gbuffer_geometry.glsl"
layout(location=0) out vec2 gPosition;
layout(location=1) out vec3 gNormal;
layout(location=2) out vec4 gColor;
layout(location=3) out vec3 gOther;
#endif
layout(binding=0) uniform sampler2D uSurface;
layout(binding=1) uniform sampler2DArray uAlbedo;
layout(binding=2) uniform sampler2DArray uNormal;
uniform int uKind;
uniform float uGamma;
in vec3 viewPosition,worldNormal;
in vec2 uv;
in vec3 color;
flat in uint fadeMask,siteHash;
flat in vec2 rotation;
flat in ivec3 captureIDs;
flat in vec3 captureWeights;
in vec3 captureUV0,captureUV1,captureUV2;
void main()
{
#if !TREE_SHADOW
    float tag=geGBufferGeometryTag(dFdx(viewPosition),dFdy(viewPosition));
#endif
    uint lodFade=fadeMask&255u,farFade=fadeMask>>9u;
    if(lodFade<255u || farFade<127u || (fadeMask&256u)!=0u)
    {
        uint h=treeHash(uint(gl_FragCoord.x)^treeHash(uint(gl_FragCoord.y)^siteHash));
        float r=float(h>>8u)*(1.0/16777216.0);
        if((fadeMask&256u)!=0u ? r<float(lodFade)/255.0 : r>=float(lodFade)/255.0) discard;
        if(farFade<127u && treeRandom(h^0x91bc3271u)>=float(farFade)/127.0) discard;
    }
    vec3 albedo,N=worldNormal;
    if(uKind==3)
    {
        vec4 material=vec4(0),normal=vec4(0);
        vec3 coords[3]=vec3[3](captureUV0,captureUV1,captureUV2);
        for(int i=0;i<3;++i)
        {
            if(captureWeights[i]<=0.0) continue;
            vec2 p=coords[i].xy/coords[i].z;
            float inside=step(0.0,p.x)*step(p.x,1.0)*step(0.0,p.y)*step(p.y,1.0);
            vec3 address=vec3(p,captureIDs[i]); float weight=captureWeights[i]*inside;
            material+=texture(uAlbedo,address)*weight;
#if !TREE_SHADOW
            normal+=texture(uNormal,address)*weight;
#endif
        }
        if(material.a<0.5) discard;
        albedo=material.rgb/material.a;
        N=treeRotate(normal.rgb/max(normal.a,0.00001)*2.0-1.0,rotation);
    }
    else
    {
        vec4 surface=texture(uSurface,uv);
        if(uKind==1 && surface.a<0.5) discard;
        albedo=color*surface.rgb;
    }
#if !TREE_SHADOW
    gPosition=geGBufferPackPosition(viewPosition,tag);
    gNormal=dot(N,N)>0.00001?normalize(N):vec3(0,1,0);
    gColor=vec4(pow(max(albedo,vec3(0)),vec3(1.0/max(uGamma,0.001))),1);
    gOther=vec3(0,uKind==0?0.9:(uKind==1?0.82:0.86),1);
#endif
}
