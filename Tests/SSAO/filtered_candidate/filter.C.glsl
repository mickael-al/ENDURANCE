#version 450 core
#include "common.glsl"
layout(local_size_x=8,local_size_y=8) in;
layout(binding=2) uniform sampler2D geSsaoRaw;
layout(r16f,binding=0) writeonly uniform image2D geSsaoOutput;

// An 8x8 output tile with a two-pixel halo. Each position/normal is decoded
// cooperatively once instead of once for every neighbour of every pixel.
const int GE_SSAO_TILE=12;
const int GE_SSAO_TILE_COUNT=144;
shared float tileAo[GE_SSAO_TILE_COUNT];
shared vec2 tileEncoded[GE_SSAO_TILE_COUNT];
shared vec4 tilePosition[GE_SSAO_TILE_COUNT];
shared vec4 tileNormal[GE_SSAO_TILE_COUNT];
shared uint tileVaries;

void main()
{
    ivec2 size=imageSize(geSsaoOutput);
    ivec2 origin=ivec2(gl_WorkGroupID.xy)*8-2;
    uint lane=gl_LocalInvocationIndex;
    if(lane==0u) tileVaries=0u;
    for(uint i=lane;i<uint(GE_SSAO_TILE_COUNT);i+=64u)
    {
        ivec2 p=origin+ivec2(int(i)%GE_SSAO_TILE,int(i)/GE_SSAO_TILE);
        bool valid=all(greaterThanEqual(p,ivec2(0))) && all(lessThan(p,size));
        tileAo[i]=valid?texelFetch(geSsaoRaw,p,0).r:1.0;
        tileEncoded[i]=valid?texelFetch(geSsaoPosition,p,0).rg:vec2(0);
    }
    barrier();
    bool different=false;
    for(uint i=lane;i<uint(GE_SSAO_TILE_COUNT);i+=64u)
        different=different || tileAo[i]!=tileAo[0];
    if(different) atomicOr(tileVaries,1u);
    barrier();
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    bool outputValid=all(lessThan(pixel,size));
    // A normalized weighted mean of identical samples is exactly that value.
    // This branch is workgroup-uniform, so even partial tiles obey all barriers.
    if(tileVaries==0u)
    {
        if(outputValid)
        {
            ivec2 local=ivec2(gl_LocalInvocationID.xy)+2;
            int index=local.y*GE_SSAO_TILE+local.x;
            float value=geSsaoReceiver(tileEncoded[index])?tileAo[0]:1.0;
            imageStore(geSsaoOutput,pixel,vec4(value));
        }
        return;
    }
    for(uint i=lane;i<uint(GE_SSAO_TILE_COUNT);i+=64u)
    {
        ivec2 p=origin+ivec2(int(i)%GE_SSAO_TILE,int(i)/GE_SSAO_TILE);
        vec2 encoded=tileEncoded[i];
        vec3 normal=vec3(0);
        bool valid=geSsaoReceiver(encoded);
        if(valid) valid=geSsaoViewNormal(p,encoded.y,normal);
        tilePosition[i]=valid?vec4(geSsaoPositionAt(p,encoded,size),1):vec4(0);
        tileNormal[i]=vec4(normal,valid?1:0);
    }
    barrier();
    if(!outputValid) return;
    ivec2 local=ivec2(gl_LocalInvocationID.xy)+2;
    int centreIndex=local.y*GE_SSAO_TILE+local.x;
    float centre=tileAo[centreIndex];
    if(!geSsaoReceiver(tileEncoded[centreIndex]))
    {
        imageStore(geSsaoOutput,pixel,vec4(1.0));
        return;
    }
    if(tilePosition[centreIndex].w==0.0)
    {
        imageStore(geSsaoOutput,pixel,vec4(centre));
        return;
    }
    // Also preserve constant subregions of a nonconstant workgroup exactly.
    bool uniformValue=true;
    for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x)
        uniformValue=uniformValue && tileAo[centreIndex+y*GE_SSAO_TILE+x]==centre;
    if(uniformValue)
    {
        imageStore(geSsaoOutput,pixel,vec4(centre));
        return;
    }
    vec3 position=tilePosition[centreIndex].xyz,normal=tileNormal[centreIndex].xyz;
    const float spatial[5]=float[5](1.0,4.0,6.0,4.0,1.0);
    float sum=0.0,weights=0.0;
    for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x)
    {
        int index=centreIndex+y*GE_SSAO_TILE+x;
        if(tilePosition[index].w==0.0) continue;
        vec3 neighbourPosition=tilePosition[index].xyz;
        if(abs(neighbourPosition.z-position.z)>GE_SSAO_RADIUS) continue;
        vec3 neighbourNormal=tileNormal[index].xyz;
        float normalWeight=max(dot(normal,neighbourNormal),0.0);
        normalWeight*=normalWeight; normalWeight*=normalWeight; normalWeight*=normalWeight;
        vec3 delta=neighbourPosition-position;
        // A fixed pixel stencil must not mix AO from farther away than
        // the effect itself, especially on distant/grazing surfaces.
        float distanceWeight=1.0-smoothstep(0.25*GE_SSAO_RADIUS*GE_SSAO_RADIUS,
            GE_SSAO_RADIUS*GE_SSAO_RADIUS,dot(delta,delta));
        float planeDistance=max(abs(dot(delta,normal)),abs(dot(delta,neighbourNormal)));
        float tolerance=0.05+8.0*1.1920928955078125e-7*abs(position.z);
        float depthWeight=max(0.0,1.0-planeDistance/tolerance);
        depthWeight*=depthWeight;
        float weight=spatial[x+2]*spatial[y+2]*normalWeight*depthWeight*distanceWeight;
        sum+=tileAo[index]*weight; weights+=weight;
    }
    imageStore(geSsaoOutput,pixel,vec4(clamp(weights>0.0?sum/weights:centre,0.0,1.0)));
}
