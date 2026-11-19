#version 450 core
#include "common.glsl"
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=4) readonly buffer UniformBufferSSAO { vec4 samples[]; } ubssao;
layout(binding=2) uniform sampler2D geSsaoNoise;
layout(r16f,binding=0) writeonly uniform image2D geSsaoOutput;
#ifndef GE_SSAO_DISABLE_SUBPIXEL_SKIP
#define GE_SSAO_DISABLE_SUBPIXEL_SKIP 0
#endif

bool geSsaoSphereInsidePixel(vec3 position,ivec2 size)
{
    // For N/W, a sphere offset d changes one projected coordinate by
    // (a*dx+(b-N/W*c)*dz)/(W+c*dz). Bound the numerator with the triangle
    // inequality and the denominator by its minimum over the entire sphere.
    // This is conservative for both Camera perspective and orthographic forms.
    float w=ubc.proj[2][3]*position.z+ubc.proj[3][3];
    float denominator=w-GE_SSAO_RADIUS*abs(ubc.proj[2][3]);
    if(!(denominator>0.0)) return false;
    vec2 diagonal=vec2(ubc.proj[0][0],ubc.proj[1][1]);
    vec2 ndc=(diagonal*position.xy+ubc.proj[2].xy*position.z+ubc.proj[3].xy)/w;
    vec2 bound=0.5*vec2(size)*GE_SSAO_RADIUS*
        (abs(diagonal)+abs(ubc.proj[2].xy-ndc*ubc.proj[2][3]))/denominator;
    // Reserve float rounding in projection/reconstruction. All valid samples
    // then hit the receiver texel and are discarded by the loop below anyway.
    vec2 rounding=8.0*1.1920928955078125e-7*vec2(size);
    return all(lessThan(bound+rounding,vec2(0.5)));
}

void main()
{
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(geSsaoOutput);
    if(any(greaterThanEqual(pixel,size))) return;
    vec2 encoded=texelFetch(geSsaoPosition,pixel,0).rg;
    float visibility=1.0;
    if(geSsaoReceiver(encoded))
    {
        vec3 position=geSsaoPositionAt(pixel,encoded,size),normal;
#if !GE_SSAO_DISABLE_SUBPIXEL_SKIP
        if(geSsaoSphereInsidePixel(position,size))
        {
            imageStore(geSsaoOutput,pixel,vec4(1.0));
            return;
        }
#endif
        if(geSsaoViewNormal(pixel,encoded.y,normal))
        {
            vec3 random=vec3(texelFetch(geSsaoNoise,pixel&ivec2(3),0).xy,0.0);
            vec3 tangent=random-normal*dot(random,normal);
            float tangentLength2=dot(tangent,tangent);
            if(!(tangentLength2>1e-8))
            {
                vec3 axis=abs(normal.z)<0.9?vec3(0,0,1):vec3(0,1,0);
                tangent=cross(axis,normal);
                tangentLength2=dot(tangent,tangent);
            }
            tangent*=inversesqrt(tangentLength2);
            mat3 basis=mat3(tangent,cross(normal,tangent),normal);
            vec2 diagonal=vec2(ubc.proj[0][0],ubc.proj[1][1]);
            float occlusion=0.0;
            for(int i=0;i<GE_SSAO_SAMPLES;++i)
            {
                vec3 samplePosition=position+(basis*ubssao.samples[i].xyz)*GE_SSAO_RADIUS;
                // Camera projections have no XY cross terms; do not multiply
                // and divide four components when only projected XY is needed.
                float w=ubc.proj[2][3]*samplePosition.z+ubc.proj[3][3];
                if(!(w>0.0) || !(samplePosition.z<0.0)) continue;
                vec2 uv=(diagonal*samplePosition.xy+ubc.proj[2].xy*samplePosition.z+
                    ubc.proj[3].xy)*(0.5/w)+0.5;
                if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) continue;
                ivec2 samplePixel=ivec2(uv*vec2(size));
                if(all(equal(samplePixel,pixel))) continue;
                vec2 neighbour=texelFetch(geSsaoPosition,samplePixel,0).rg;
                if(!geSsaoGeometry(neighbour) || neighbour.x<samplePosition.z+GE_SSAO_BIAS) continue;
                float separation=abs(position.z-neighbour.x);
                // Finite support avoids foreground/background halos beyond the AO
                // radius; radius/separation never reaches zero at any distance.
                float rangeWeight=1.0-smoothstep(0.5*GE_SSAO_RADIUS,GE_SSAO_RADIUS,separation);
                occlusion+=rangeWeight;
            }
            visibility=clamp(1.0-occlusion/float(GE_SSAO_SAMPLES),0.0,1.0);
        }
    }
    imageStore(geSsaoOutput,pixel,vec4(visibility));
}
