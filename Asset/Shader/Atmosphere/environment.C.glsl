#version 450 core
#include "atmosphere_common.glsl"
#include "cubemap_common.glsl"
#include "moon_surface.glsl"
#ifdef GE_PHYSICAL_SKY
#include "sky_evaluate.glsl"
#endif
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform imageCube geEnvironment;
layout(rgba16f,binding=1) writeonly uniform imageCube geLightingEnvironment;
#if GE_ATM_MOON_SURFACE
layout(rgba16f,binding=2) writeonly uniform imageCube geReflectionEnvironment;
#endif
layout(binding=9) uniform samplerCube geExternalEnvironment;
uniform int geUseExternalEnvironment;
uniform ivec3 geDispatchOffset; // tile XY and face; zero also supports a complete dispatch

vec3 geFallbackDisk(vec3 d,vec4 direction,vec4 disk)
{
    if(direction.w<0.5) return vec3(0);
    return disk.rgb*geAtmDiskMask(d,direction.xyz,disk.w);
}
void main()
{
    ivec3 p=ivec3(gl_GlobalInvocationID)+geDispatchOffset;
    ivec2 size=imageSize(geEnvironment);
    if(any(greaterThanEqual(p.xy,size))||p.z>=6) return;
    vec3 d=geCubeDirection(p,size);
    vec3 visible,lighting,lunar=vec3(0);
    if(geUseExternalEnvironment!=0)
    {
        lighting=geAtmArtistic(textureLod(geExternalEnvironment,d,0.0).rgb,d);
#ifdef GE_PHYSICAL_SKY
        // Match the analytic visible disks even for a caller-owned source:
        // transmittance, clouds and artistic correction have identical rules.
        lunar=geSkyMoon(d);
        visible=lighting+geSkySunAndLegacyMoon(d)+lunar;
#else
        visible=lighting+geFallbackDisk(d,geAtmSunDirection,geAtmSunDisk)
            +geFallbackDisk(d,geAtmMoonDirection,geAtmMoonDisk);
#endif
    }
    else
    {
#ifdef GE_PHYSICAL_SKY
        float cloudTransmission;
        lighting=geSkyRadiance(d,false,true,cloudTransmission);
        lunar=geSkyMoon(d,cloudTransmission);
        visible=lighting+geSkySunAndLegacyMoon(d)+lunar;
#else
        lighting=geAtmPalette(d)*max(geAtmArt.x,0.0);
        visible=lighting+geFallbackDisk(d,geAtmSunDirection,geAtmSunDisk)
            +geFallbackDisk(d,geAtmMoonDirection,geAtmMoonDisk);
#endif
    }
    // Diffuse excludes an explicitly assigned moon light (no double energy).
    // GGX contains its extended surface, instead of a directional point glint.
#if GE_ATM_MOON_SURFACE
#ifndef GE_PHYSICAL_SKY
    lunar=geAtmArtistic(geMoonRadiance(d),d);
    visible=lighting+lunar+geFallbackDisk(d,geAtmSunDirection,geAtmSunDisk)*(1.0-geMoonCoverage(d));
#endif
    imageStore(geReflectionEnvironment,p,vec4(min(max(lighting+lunar,vec3(0)),vec3(65000)),1));
#endif
    // The solar disk remains excluded from diffuse and specular IBL.
    imageStore(geEnvironment,p,vec4(min(max(visible,vec3(0)),vec3(65000)),1));
    imageStore(geLightingEnvironment,p,vec4(min(max(lighting,vec3(0)),vec3(65000)),1));
}
