// Reusable atmosphere contract. Distances are metres, coefficients are 1/metre,
// colours are linear HDR radiance. No biome or game coordinate convention lives
// here. Keep this std140 layout identical to AtmosphereGpuParameters in C++.
#ifndef GE_ATMOSPHERE_COMMON_GLSL
#define GE_ATMOSPHERE_COMMON_GLSL

// Atmosphere-owned programs specialize only the scene feature switches.
// Shared material includes retain the uniform fallback (-1): their atmosphere
// may become ready or disappear independently of the material's lifetime.
#ifndef GE_ATM_PHYSICAL
#define GE_ATM_PHYSICAL -1
#endif
#ifndef GE_ATM_LOCAL_FOG
#define GE_ATM_LOCAL_FOG -1
#endif
#ifndef GE_ATM_CLOUDS
#define GE_ATM_CLOUDS -1
#endif
#ifndef GE_ATM_TERRAIN_SHADOWS
#define GE_ATM_TERRAIN_SHADOWS -1
#endif
#if GE_ATM_PHYSICAL >= 0
#define GE_ATM_PHYSICAL_ON (GE_ATM_PHYSICAL != 0)
#else
#define GE_ATM_PHYSICAL_ON (geAtmControl.x != 0)
#endif
#if GE_ATM_LOCAL_FOG >= 0
#define GE_ATM_LOCAL_FOG_ON (GE_ATM_LOCAL_FOG != 0)
#else
#define GE_ATM_LOCAL_FOG_ON (geAtmControl.y != 0)
#endif
#if GE_ATM_CLOUDS >= 0
#define GE_ATM_CLOUDS_ON (GE_ATM_CLOUDS != 0)
#else
#define GE_ATM_CLOUDS_ON (geAtmControl.z != 0)
#endif
#if GE_ATM_TERRAIN_SHADOWS >= 0
#define GE_ATM_TERRAIN_SHADOWS_ON (GE_ATM_TERRAIN_SHADOWS != 0)
#else
#define GE_ATM_TERRAIN_SHADOWS_ON (geAtmControl.w != 0)
#endif

const float GE_ATM_PI = 3.14159265358979323846;
layout(std140, binding=6) uniform GeAtmosphereParameters
{
    vec4 geAtmRadii;          // bottom radius, top radius, sea level in world Y, reserved
    vec4 geAtmRayleigh;       // scattering RGB, density scale height
    vec4 geAtmMie;            // scattering RGB, density scale height
    vec4 geAtmAbsorption;     // ozone absorption RGB, ozone centre height
    vec4 geAtmMedium;         // Mie absorption, Mie g, ozone half width, multiple scattering strength
    vec4 geAtmSunDirection;   // world direction TOWARDS source, enabled
    vec4 geAtmSunRadiance;    // source irradiance RGB, reserved
    vec4 geAtmSunDisk;        // independently editable disk radiance RGB, angular radius radians
    vec4 geAtmMoonDirection;  // optional second source, enabled
    vec4 geAtmMoonRadiance;   // second source irradiance RGB, reserved
    vec4 geAtmMoonDisk;       // visible second disk radiance RGB, angular radius radians
    vec4 geAtmHorizon;        // artistic horizon RGB, correction strength
    vec4 geAtmMiddle;         // artistic middle RGB, middle elevation sin(angle)
    vec4 geAtmZenith;         // artistic zenith RGB, gradient exponent
    vec4 geAtmArt;            // exposure, visible saturation, IBL saturation, ground albedo
    vec4 geAtmCamera;         // world XYZ, continuously advancing time seconds
    vec4 geAtmVolume;         // max camera volume distance, fog phase g, generic fog density, fog base height
    vec4 geAtmFog;            // generic fog scattering albedo RGB, height scale
    vec4 geAtmFogNoise;       // reserved; structured game fog is provided by geGameFog
    vec4 geAtmCloudLayer;     // bottom altitude, thickness, coverage, extinction density
    vec4 geAtmCloudShape;     // shape scale metres, detail frequency multiplier, erosion, phase g
    vec4 geAtmCloudOffset;    // accumulated displacement X,Z metres, shadow map half extent, reserved
    vec4 geAtmShadowOrigin;   // cloud shadow world centre X,Z, reserved, reserved
    ivec4 geAtmShadow;        // sun shadow first layer, moon first layer, total shadow layers, reserved
    ivec4 geAtmQuality;       // sky steps, transmittance steps, multiple-scatter directions, MS steps
    ivec4 geAtmSamples;       // volume steps/slice, cloud light steps, shadow steps, cloud sky steps
    ivec4 geAtmControl;       // physical atmosphere, local fog, clouds, terrain shadows (0/1)
    uvec4 geAtmMisc;          // procedural seed, frame index, reserved, reserved
    vec4 geAtmCloudWeather;  // regional scale metres, coverage variation, contrast, profile variation
    vec4 geAtmCloudProfiles; // stratus, cumulus and tower weights, reserved
    mat4 geAtmInvView;
    mat4 geAtmInvProjection;
    mat4 geAtmView;
};

#ifdef GE_CUSTOM_RADIANCE
vec3 geCustomizeRadiance(vec3 radiance,vec3 direction);
#endif

// One celestial light at a time. Keep the raw source/disk parameters intact:
// the visible moon still reflects sunlight when the sun is below our horizon.
// These predicates are uniform across a dispatch/draw (no pixel divergence).
bool geAtmSunAboveHorizon()
{
    return geAtmSunDirection.w>0.5 && geAtmSunDirection.y>=0.0;
}
bool geAtmSunLightingEnabled()
{
    return geAtmSunAboveHorizon() && any(greaterThan(geAtmSunRadiance.rgb,vec3(0)));
}
bool geAtmMoonLightingEnabled()
{
    return !geAtmSunAboveHorizon() && geAtmMoonDirection.w>0.5
        && geAtmMoonDirection.y>=0.0 && any(greaterThan(geAtmMoonRadiance.rgb,vec3(0)));
}

float geAtmLuminance(vec3 c) { return dot(c,vec3(0.2126,0.7152,0.0722)); }
vec3 geAtmSaturate(vec3 c,float saturation)
{
    return max(mix(vec3(geAtmLuminance(c)),c,max(saturation,0.0)),vec3(0.0));
}

float geAtmRayleighPhase(float mu) { return 3.0*(1.0+mu*mu)/(16.0*GE_ATM_PI); }
float geAtmMiePhase(float mu,float g)
{
    g=clamp(g,-0.95,0.95);
    return (1.0-g*g)/(4.0*GE_ATM_PI*pow(max(1.0+g*g-2.0*g*mu,0.001),1.5));
}

float geAtmDiskMask(vec3 direction,vec3 sourceDirection,float angularRadius)
{
    if(angularRadius<=0.0) return 0.0;
    // Chord distance remains resolvable for tiny editable disks. Comparing two
    // cosines close to one can round both smoothstep edges to exactly 1.0.
    float chord=2.0*sin(0.5*clamp(angularRadius,1e-6,0.5));
    return 1.0-smoothstep(chord*0.96,chord*1.04,length(direction-sourceDirection));
}

// For an ORTHOGRAPHIC directional shadow matrix only. The row norm converts
// a world-space distance into its [0,1] depth difference and remains valid
// when a large upstream caster margin expands the cascade's depth span.
float geAtmDirectionalShadowBias(mat4 projectionView,float worldMetres)
{
    vec3 depthRow=vec3(projectionView[0][2],projectionView[1][2],projectionView[2][2]);
    return max(worldMetres,0.0)*0.5*length(depthRow);
}

vec3 geAtmPalette(vec3 direction)
{
    float e=clamp(direction.y,0.0,1.0);
    float middle=clamp(geAtmMiddle.w,0.01,0.99);
    float exponent=max(geAtmZenith.w,0.05);
    // Shape each interval independently, so changing the exponent does not
    // move the explicitly selected middle elevation.
    if(e<middle)
        return mix(geAtmHorizon.rgb,geAtmMiddle.rgb,smoothstep(0.0,1.0,pow(e/middle,exponent)));
    return mix(geAtmMiddle.rgb,geAtmZenith.rgb,
        smoothstep(0.0,1.0,pow((e-middle)/(1.0-middle),exponent)));
}

// Multiplicative artistic tint preserves zero illumination and all extinction.
// Linear RGB 0.5 is the neutral artist reference. The magnitude is intentional:
// a darker gradient stop darkens its band, and HDR colours can raise radiance.
// The same function is used for sky, environment filtering and in-scattering.
vec3 geAtmArtistic(vec3 radiance,vec3 direction)
{
    vec3 tint=geAtmPalette(direction);
    vec3 coloured=radiance*(tint/0.5);
    vec3 result=max(mix(radiance,coloured,clamp(geAtmHorizon.w,0.0,1.0))*max(geAtmArt.x,0.0),vec3(0.0));
#ifdef GE_CUSTOM_RADIANCE
    result=max(geCustomizeRadiance(result,direction),vec3(0.0));
#endif
    return result;
}

vec3 geAtmOrigin()
{
    // A local planet frame follows XZ; camera Y remains physical altitude.
    return vec3(0.0,max(geAtmRadii.x+geAtmCamera.y-geAtmRadii.z,geAtmRadii.x+1.0),0.0);
}

// Ordered roots, negative pair means no intersection. The q/c formulation
// avoids cancellation on long, nearly tangent rays.
vec2 geAtmSphere(vec3 origin,vec3 direction,float radius)
{
    float b=dot(origin,direction);
    float c=(length(origin)-radius)*(length(origin)+radius);
    float d=b*b-c;
    if(d<0.0) return vec2(-1.0);
    float s=sqrt(max(d,0.0));
    float q=-b-(b>=0.0 ? s : -s);
    if(abs(q)<0.0001) return vec2(-b-s,-b+s);
    float a=q, z=c/q;
    return vec2(min(a,z),max(a,z));
}

vec2 geAtmPath(vec3 origin,vec3 direction)
{
    vec2 top=geAtmSphere(origin,direction,geAtmRadii.y);
    if(top.y<=0.0) return vec2(0.0);
    float start=max(0.0,top.x), end=top.y;
    vec2 ground=geAtmSphere(origin,direction,geAtmRadii.x);
    if(ground.x>start) end=min(end,ground.x);
    return vec2(start,max(start,end));
}

void geAtmMediumAt(vec3 planetPosition,out vec3 rayleigh,out vec3 mie,out vec3 extinction)
{
    float h=max(length(planetPosition)-geAtmRadii.x,0.0);
    if(!GE_ATM_PHYSICAL_ON || h>geAtmRadii.y-geAtmRadii.x)
    {
        rayleigh=vec3(0.0); mie=vec3(0.0); extinction=vec3(0.0); return;
    }
    float r=exp(-h/max(geAtmRayleigh.w,1.0));
    float m=exp(-h/max(geAtmMie.w,1.0));
    float ozone=max(1.0-abs(h-geAtmAbsorption.w)/max(geAtmMedium.z,1.0),0.0);
    rayleigh=max(geAtmRayleigh.rgb,vec3(0.0))*r;
    mie=max(geAtmMie.rgb,vec3(0.0))*m;
    extinction=rayleigh+mie+max(geAtmMedium.x,0.0)*m+max(geAtmAbsorption.rgb,vec3(0.0))*ozone;
}

vec3 geAtmIntegral(vec3 extinction,float distance)
{
    vec3 x=extinction*max(distance,0.0);
    // Small-x series avoids losing optical depth to exp(x)==1 in float32.
    vec3 exact=(vec3(1.0)-exp(-x))/max(extinction,vec3(1e-20));
    vec3 series=max(distance,0.0)*(vec3(1.0)-0.5*x+x*x/6.0);
    return mix(exact,series,lessThan(x,vec3(0.001)));
}

void geAtmIntegrate(inout vec3 scattering,inout vec3 transmittance,
                    vec3 source,vec3 extinction,float distance)
{
    scattering+=transmittance*max(source,vec3(0.0))*geAtmIntegral(extinction,distance);
    transmittance*=exp(-max(extinction,vec3(0.0))*max(distance,0.0));
}

// Latitude compression dedicates samples to the horizon. Longitude wraps;
// sky texture uses GL_REPEAT in X and GL_CLAMP_TO_EDGE in Y.
vec2 geAtmSkyUV(vec3 direction)
{
    float latitude=asin(clamp(direction.y,-1.0,1.0));
    return vec2(atan(direction.z,direction.x)/(2.0*GE_ATM_PI)+0.5,
        0.5+0.5*sign(latitude)*sqrt(abs(latitude)/(0.5*GE_ATM_PI)));
}

vec3 geAtmSkyDirection(vec2 uv)
{
    float x=uv.y*2.0-1.0;
    float latitude=sign(x)*x*x*(GE_ATM_PI*0.5);
    float azimuth=(uv.x-0.5)*(GE_ATM_PI*2.0);
    return vec3(cos(latitude)*cos(azimuth),sin(latitude),cos(latitude)*sin(azimuth));
}

vec3 geAtmViewDirection(vec2 uv)
{
    vec4 p=geAtmInvProjection*vec4(uv*2.0-1.0,1.0,1.0);
    return normalize(mat3(geAtmInvView)*normalize(p.xyz/max(abs(p.w),1e-8)));
}

float geAtmSliceDistance(int slice,int count)
{
    float f=float(slice)/float(max(count-1,1));
    return max(geAtmVolume.x,1.0)*f*f;
}

#endif
