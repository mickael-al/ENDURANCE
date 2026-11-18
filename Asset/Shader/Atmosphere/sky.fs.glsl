#version 450 core
#include "atmosphere_common.glsl"
#ifdef GE_PHYSICAL_SKY
#define GE_ATM_TRANSMITTANCE_BINDING 15
#define GE_ATM_MULTISCATTER_BINDING 11
#define GE_ATM_CLOUD_SHADOW_BINDING 14
#include "sky_evaluate.glsl"
#endif
layout(binding=0) uniform samplerCube geDisplayedEnvironment;
layout(binding=1) uniform sampler2D geAtmVisibleClouds;
uniform bool geCurrentCloudView;
uniform bool geRawExternalEnvironment;
in vec2 geSkyUV;
layout(location=0) out vec4 gPosition;
layout(location=1) out vec3 gNormal;
layout(location=2) out vec4 gColor;
layout(location=3) out vec3 gOther;
void main()
{
    vec3 direction=geAtmViewDirection(geSkyUV);
    vec3 color=textureLod(geDisplayedEnvironment,direction,0).rgb;
    // A caller-owned complete source can be displayed while its first IBL
    // transaction is pending. Published environments already carry this tint.
    if(geRawExternalEnvironment) color=geAtmArtistic(color,direction);
    #ifdef GE_PHYSICAL_SKY
    if(geCurrentCloudView)
        color=max(textureLod(geAtmSkyView,geAtmSkyUV(direction),0.0).rgb
            +textureLod(geAtmVisibleClouds,geSkyUV,0.0).rgb,vec3(0.0));
    // Resolve sub-texel angular disks analytically at display resolution. The
    // complete public cubemap still contains them. Visible clouds use the
    // current camera view; the independent IBL transaction may be older.
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
    gColor=vec4(color,1);
    gOther=vec3(0);
}
