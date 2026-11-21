#version 450 core
#include "medium.glsl"
layout(local_size_x=4,local_size_y=4,local_size_z=4) in;
layout(rgba32f,binding=0) writeonly uniform image3D geAtmSourceOutput;
void main()
{
    ivec3 p=ivec3(gl_GlobalInvocationID),size=imageSize(geAtmSourceOutput);
    if(any(greaterThanEqual(p,size))) return;
    vec3 source=vec3(0.0); float extinction=0.0;
    if(p.z>0 && geAtmHasClouds())
    {
        vec3 direction=geAtmViewDirection((vec2(p.xy)+0.5)/vec2(size.xy));
        float begin=geAtmSliceDistance(p.z-1,size.z),end=geAtmSliceDistance(p.z,size.z);
        float distance=0.5*(begin+end);
        vec3 world=geAtmCamera.xyz+direction*distance;
        float footprint=max(distance*2.0/float(size.y),(end-begin)*0.25);
        extinction=geAtmCloudDensityAtFootprint(world,footprint);
        if(extinction>0.0)
        {
            vec2 visibility=vec2(1.0);
            if(geAtmSunDirection.w>0.5)
                visibility.x=geAtmTerrainVisibility(world,geAtmShadow.x)
                    *geAtmCloudLightRay(world,normalize(geAtmSunDirection.xyz),geAtmSamples.y);
            if(geAtmMoonDirection.w>0.5)
                visibility.y=geAtmTerrainVisibility(world,geAtmShadow.y)
                    *geAtmCloudLightRay(world,normalize(geAtmMoonDirection.xyz),geAtmSamples.y);
            source=geAtmMediumLight(world,geAtmOrigin()+direction*distance,direction,
                vec3(extinction*0.98),geAtmCloudShape.w,visibility);
        }
    }
    // Achromatic extinction fits the unused source alpha at unchanged FP32
    // precision, removing a texture allocation and a separate write/fetch.
    imageStore(geAtmSourceOutput,p,vec4(source,extinction));
}
