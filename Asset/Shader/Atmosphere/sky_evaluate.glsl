#ifndef GE_ATMOSPHERE_SKY_EVALUATE_GLSL
#define GE_ATMOSPHERE_SKY_EVALUATE_GLSL
#include "physical.glsl"
#include "cloud_common.glsl"
layout(binding=2) uniform sampler2D geAtmSkyView;

vec3 geAtmSkyCloudSource(vec3 world,vec3 planetPosition,vec3 direction,float extinction)
{
    vec3 source=vec3(0.0);
    if(geAtmSunDirection.w>0.5)
    {
        vec3 light=normalize(geAtmSunDirection.xyz);
        float visibility=geAtmCloudLightRay(world,light,geAtmSamples.y);
        vec3 direct=geAtmSunTransmittance(planetPosition,light)
            *geAtmMiePhase(dot(direction,light),geAtmCloudShape.w)*visibility;
        vec3 ambient=geAtmMultipleScattering(planetPosition,light)*max(geAtmMedium.w,0.0);
        source+=extinction*0.98*geAtmSunRadiance.rgb*(direct+ambient);
    }
    if(geAtmMoonDirection.w>0.5)
    {
        vec3 light=normalize(geAtmMoonDirection.xyz);
        float visibility=geAtmCloudLightRay(world,light,geAtmSamples.y);
        vec3 direct=geAtmSunTransmittance(planetPosition,light)
            *geAtmMiePhase(dot(direction,light),geAtmCloudShape.w)*visibility;
        vec3 ambient=geAtmMultipleScattering(planetPosition,light)*max(geAtmMedium.w,0.0);
        source+=extinction*0.98*geAtmMoonRadiance.rgb*(direct+ambient);
    }
    return geAtmArtistic(source,direction);
}

vec3 geSkyDisks(vec3 direction)
{
    vec3 result=vec3(0.0),origin=geAtmOrigin();
    if(geAtmSunDirection.w>0.5 && geAtmSunDisk.w>0.0)
    {
        vec3 light=normalize(geAtmSunDirection.xyz);
        float disk=geAtmDiskMask(direction,light,geAtmSunDisk.w);
        if(disk>0.0)
            result+=geAtmSunDisk.rgb*disk*geAtmSunTransmittance(origin,light)
                *geAtmCloudLightRay(geAtmCamera.xyz,light,geAtmSamples.z);
    }
    if(geAtmMoonDirection.w>0.5 && geAtmMoonDisk.w>0.0)
    {
        vec3 light=normalize(geAtmMoonDirection.xyz);
        float disk=geAtmDiskMask(direction,light,geAtmMoonDisk.w);
        if(disk>0.0)
            result+=geAtmMoonDisk.rgb*disk*geAtmSunTransmittance(origin,light)
                *geAtmCloudLightRay(geAtmCamera.xyz,light,geAtmSamples.z);
    }
    return geAtmArtistic(result,direction);
}

vec3 geSkyRadiance(vec3 direction,bool includeDisk,bool includeClouds)
{
    direction=normalize(direction);
    vec3 sky=max(textureLod(geAtmSkyView,geAtmSkyUV(direction),0.0).rgb,vec3(0.0));
    if(includeClouds && geAtmControl.z!=0 && geAtmCloudLayer.z>0.0 && geAtmCloudLayer.w>0.0)
    {
        float maximum=min(120000.0,max(geAtmCloudShape.x,1.0)*16.0);
        vec2 path=geAtmCloudLayerPath(geAtmCamera.xyz,direction,maximum);
        // The planetary ground bounds rays into the lower hemisphere.
        vec2 planetPath=geAtmPath(geAtmOrigin(),direction);
        path.y=min(path.y,planetPath.y);
        if(path.y>path.x)
        {
            vec3 baselineS=vec3(0.0),baselineT=vec3(1.0);
            vec3 combinedS=vec3(0.0),combinedT=vec3(1.0);
            bool cloudEncountered=false;
            // Clear-air prefix is smooth: reserve the actual cloud-step budget
            // for the layer, instead of spending most samples below its base.
            if(path.x>0.0)
            {
                for(int i=0;i<8;++i)
                {
                    float a=float(i)/8.0,b=float(i+1)/8.0;
                    float begin=path.x*a*a,end=path.x*b*b;
                    vec3 planet=geAtmOrigin()+direction*((begin+end)*0.5);
                    vec3 e;
                    vec3 physical=geAtmPhysicalSource(planet,direction,vec2(1.0),e);
                    geAtmIntegrate(baselineS,baselineT,physical,e,end-begin);
                }
                combinedS=baselineS; combinedT=baselineT;
            }
            int samples=clamp(geAtmSamples.w,8,256);
            for(int i=0;i<samples;++i)
            {
                float a=float(i)/float(samples),b=float(i+1)/float(samples);
                float begin=mix(path.x,path.y,a),end=mix(path.x,path.y,b);
                float distance=(begin+end)*0.5;
                vec3 world=geAtmCamera.xyz+direction*distance;
                vec3 planet=geAtmOrigin()+direction*distance;
                vec3 physicalExtinction;
                vec3 physical=geAtmPhysicalSource(planet,direction,vec2(1.0),physicalExtinction);
                float cloud=geAtmCloudDensityAtFootprint(world,(end-begin)*0.5);
                cloudEncountered=cloudEncountered || cloud>0.0;
                vec3 cloudSource=cloud>0.0 ? geAtmSkyCloudSource(world,planet,direction,cloud) : vec3(0.0);
                geAtmIntegrate(baselineS,baselineT,physical,physicalExtinction,end-begin);
                geAtmIntegrate(combinedS,combinedT,physical+cloudSource,physicalExtinction+vec3(cloud),end-begin);
            }
            // Recover only the attenuated tail contribution; this ratio form
            // cannot amplify numerical noise when baseline transmission is tiny.
            if(cloudEncountered)
                sky=max(combinedS+(sky-baselineS)
                    *clamp(combinedT/max(baselineT,vec3(1e-5)),vec3(0.0),vec3(1.0)),vec3(0.0));
        }
    }
    if(includeDisk) sky+=geSkyDisks(direction);
    return max(sky,vec3(0.0));
}
#endif
