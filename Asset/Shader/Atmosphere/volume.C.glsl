#version 450 core
#include "medium.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba16f,binding=0) writeonly uniform image3D geAtmScatteringOutput;
layout(rgba16f,binding=1) writeonly uniform image3D geAtmTransmissionOutput;
layout(rgba16f,binding=2) writeonly uniform image2D geAtmBaselineScatteringOutput;
layout(rgba16f,binding=3) writeonly uniform image2D geAtmBaselineTransmissionOutput;
layout(binding=9) uniform sampler3D geAtmPhysicalSourceVolume;
layout(binding=10) uniform sampler3D geAtmPhysicalExtinctionVolume;
layout(binding=11) uniform sampler3D geAtmCloudSourceVolume;
void main()
{
    ivec2 p=ivec2(gl_GlobalInvocationID.xy); ivec3 size=imageSize(geAtmScatteringOutput);
    if(any(greaterThanEqual(p,size.xy))) return;
    vec3 direction=geAtmViewDirection((vec2(p)+0.5)/vec2(size.xy));
    vec3 scattering=vec3(0.0),transmittance=vec3(1.0);
    vec3 baselineS=vec3(0.0),baselineT=vec3(1.0);
    imageStore(geAtmScatteringOutput,ivec3(p,0),vec4(0.0));
    imageStore(geAtmTransmissionOutput,ivec3(p,0),vec4(1.0));
    int steps=clamp(geAtmSamples.x,1,8);
    for(int z=1;z<size.z;++z)
    {
        float begin=geAtmSliceDistance(z-1,size.z),end=geAtmSliceDistance(z,size.z);
        // Disabled media use tiny neutral allocations; never fetch them with
        // coordinates belonging to the full camera volume.
        vec3 source=vec3(0.0),extinction=vec3(0.0);
        if(geAtmControl.x!=0)
        {
            vec4 physical=texelFetch(geAtmPhysicalSourceVolume,ivec3(p,z),0);
            source=physical.rgb;
            extinction=vec3(physical.a,texelFetch(geAtmPhysicalExtinctionVolume,ivec3(p,z),0).rg);
        }
        // The cubemap contains an unoccluded physical sky. Separate this
        // baseline from the terrain-shadowed source used in the view volume.
        float center=(begin+end)*0.5;
        vec3 unusedExtinction;
        vec3 baselineSource=geAtmPhysicalSource(geAtmOrigin()+direction*center,
            direction,vec2(1.0),unusedExtinction);
        if(geAtmControl.z!=0)
        {
            vec4 cloud=texelFetch(geAtmCloudSourceVolume,ivec3(p,z),0);
            vec3 cloudSource=cloud.rgb;
            float cloudExtinction=cloud.a;
            source+=cloudSource;
            vec3 unoccludedCloudSource=cloudSource;
            if(cloudExtinction>0.0 && geAtmControl.w!=0)
            {
                vec3 world=geAtmCamera.xyz+direction*center;
                vec2 terrain=vec2(1.0);
                if(geAtmSunDirection.w>0.5) terrain.x=geAtmTerrainVisibility(world,geAtmShadow.x);
                if(geAtmMoonDirection.w>0.5) terrain.y=geAtmTerrainVisibility(world,geAtmShadow.y);
                if(any(lessThan(terrain,vec2(1.0))))
                {
                    // The captured/current background includes cloud self-shadow,
                    // but no terrain shadow. Subtract precisely that reference,
                    // otherwise the terrain's darkening cancels from the sky.
                    vec2 cloudVisibility=vec2(1.0);
                    if(geAtmSunDirection.w>0.5)
                        cloudVisibility.x=geAtmCloudLightRay(world,normalize(geAtmSunDirection.xyz),geAtmSamples.y);
                    if(geAtmMoonDirection.w>0.5)
                        cloudVisibility.y=geAtmCloudLightRay(world,normalize(geAtmMoonDirection.xyz),geAtmSamples.y);
                    unoccludedCloudSource=geAtmMediumLight(world,geAtmOrigin()+direction*center,direction,
                        vec3(cloudExtinction*0.98),geAtmCloudShape.w,cloudVisibility);
                }
            }
            baselineSource+=unoccludedCloudSource;
            extinction+=vec3(cloudExtinction);
        }
        geAtmIntegrate(baselineS,baselineT,baselineSource,extinction,end-begin);
        float stepLength=(end-begin)/float(steps);
        for(int i=0;i<steps;++i)
        {
            float distance=begin+(float(i)+0.5)*stepLength;
            vec3 world=geAtmCamera.xyz+direction*distance;
            vec4 fog=geAtmLocalFog(world);
            vec3 localSource=source;
            if(fog.a>0.0)
                localSource+=geAtmMediumLight(world,geAtmOrigin()+direction*distance,direction,
                    fog.rgb,geAtmVolume.y,geAtmVisibility(world));
            // Extinction and scattering are added BEFORE integration, so
            // overlapping atmosphere, fog and cloud never count the path twice.
            geAtmIntegrate(scattering,transmittance,localSource,extinction+vec3(fog.a),stepLength);
        }
        imageStore(geAtmScatteringOutput,ivec3(p,z),vec4(scattering,1.0));
        imageStore(geAtmTransmissionOutput,ivec3(p,z),vec4(transmittance,1.0));
    }
    imageStore(geAtmBaselineScatteringOutput,p,vec4(baselineS,1.0));
    imageStore(geAtmBaselineTransmissionOutput,p,vec4(baselineT,1.0));
}
