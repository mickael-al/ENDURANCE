#version 450 core
#include "atmosphere_common.glsl"
#ifdef GE_PHYSICAL_SKY
#define GE_ATM_TRANSMITTANCE_BINDING 15
#define GE_ATM_MULTISCATTER_BINDING 11
#define GE_ATM_CLOUD_SHADOW_BINDING 14
#include "sky_evaluate.glsl"
#endif
layout(binding=0) uniform samplerCube geDisplayedEnvironment;
in vec2 geSkyUV;
layout(location=0) out vec4 gPosition;
layout(location=1) out vec3 gNormal;
layout(location=2) out vec4 gColor;
layout(location=3) out vec3 gOther;
void main()
{
    vec3 direction=geAtmViewDirection(geSkyUV);
    vec3 color=textureLod(geDisplayedEnvironment,direction,0).rgb;
    #ifdef GE_PHYSICAL_SKY
    // Resolve sub-texel angular disks analytically at display resolution. The
    // complete public cubemap still contains them; this companion is disk-free.
    color+=geSkyDisks(direction);
#else
    for(int body=0;body<2;++body)
    {
        vec4 source=body==0?geAtmSunDirection:geAtmMoonDirection;
        vec4 disk=body==0?geAtmSunDisk:geAtmMoonDisk;
        if(source.w>0.5)
            color+=disk.rgb*geAtmDiskMask(direction,source.xyz,disk.w);
    }
#endif
    // Dedicated negative marker bypasses material gamma and direct lighting.
    // The HDR G-buffer carries sky radiance unchanged to deferred composition.
    gPosition=vec4(0,0,0,-2);
    gNormal=vec3(0);
    gColor=vec4(geAtmSaturate(color,geAtmArt.y),1);
    gOther=vec3(0);
}
