// Shared deterministic world field. No chunk- or LOD-dependent noise frequency.
// Positive density is solid; negative density is air. This is not an SDF.
#ifndef ENDURANCE_WORLD_FIELD_GLSL
#define ENDURANCE_WORLD_FIELD_GLSL

uniform uint uCells;
uniform ivec3 uOrigin;
uniform uint uLod;
uniform float uBaseSpacing;
uniform uint uSeed;

uniform uint uFieldMode;
uniform float uBaseHeight;
uniform float uHeightAmplitude;
uniform float uHeightFrequency;
uniform float uClimateFrequency;
uniform float uBandThickness;
uniform float uBandVariation;
uniform float uBandOffset;
uniform float uBandOffsetVariation;
uniform float uDetailScale;
uniform vec3 uSphereCenter;
uniform float uSphereRadius;

struct FieldColumn
{
    vec4 terrain; // height, humidity, temperature, total band thickness
    vec4 volume;  // vertical offset, fine/coarse 3D noise weights, transition
};

uint fieldHash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

uint fieldHash2(ivec2 p, uint salt)
{
    return fieldHash(uint(p.x) * 0x9e3779b9u ^ uint(p.y) * 0x85ebca6bu ^ uSeed ^ salt);
}

uint fieldHash3(ivec3 p, uint salt)
{
    return fieldHash(uint(p.x) * 0x9e3779b9u ^ uint(p.y) * 0x85ebca6bu ^
        uint(p.z) * 0xc2b2ae35u ^ uSeed ^ salt);
}

vec2 fieldFade(vec2 f) { return f * f * f * (f * (f * 6.0 - 15.0) + 10.0); }
vec3 fieldFade(vec3 f) { return f * f * f * (f * (f * 6.0 - 15.0) + 10.0); }

float fieldGradient2(uint h, vec2 p)
{
    // Eight fixed directions: no transcendental hash and no gradient table fetch.
    uint g = h & 7u;
    if (g < 4u)
        return ((g & 1u) == 0u ? p.x : -p.x) + ((g & 2u) == 0u ? p.y : -p.y);
    return g == 4u ? p.x : g == 5u ? -p.x : g == 6u ? p.y : -p.y;
}

float fieldNoise2(vec2 p, uint salt)
{
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p), u = fieldFade(f);
    float a = fieldGradient2(fieldHash2(i, salt), f);
    float b = fieldGradient2(fieldHash2(i + ivec2(1,0), salt), f - vec2(1,0));
    float c = fieldGradient2(fieldHash2(i + ivec2(0,1), salt), f - vec2(0,1));
    float d = fieldGradient2(fieldHash2(i + ivec2(1,1), salt), f - vec2(1,1));
    return clamp(mix(mix(a,b,u.x), mix(c,d,u.x), u.y), -1.0, 1.0);
}

float fieldGradient3(uint h, vec3 p)
{
    h &= 15u;
    float a = h < 8u ? p.x : p.y;
    float b = h < 4u ? p.y : (h == 12u || h == 14u ? p.x : p.z);
    return ((h & 1u) == 0u ? a : -a) + ((h & 2u) == 0u ? b : -b);
}

float fieldNoise3(vec3 p, uint salt)
{
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p), u = fieldFade(f);
    float n000 = fieldGradient3(fieldHash3(i, salt), f);
    float n100 = fieldGradient3(fieldHash3(i+ivec3(1,0,0),salt), f-vec3(1,0,0));
    float n010 = fieldGradient3(fieldHash3(i+ivec3(0,1,0),salt), f-vec3(0,1,0));
    float n110 = fieldGradient3(fieldHash3(i+ivec3(1,1,0),salt), f-vec3(1,1,0));
    float n001 = fieldGradient3(fieldHash3(i+ivec3(0,0,1),salt), f-vec3(0,0,1));
    float n101 = fieldGradient3(fieldHash3(i+ivec3(1,0,1),salt), f-vec3(1,0,1));
    float n011 = fieldGradient3(fieldHash3(i+ivec3(0,1,1),salt), f-vec3(0,1,1));
    float n111 = fieldGradient3(fieldHash3(i+ivec3(1,1,1),salt), f-vec3(1,1,1));
    return clamp(mix(mix(mix(n000,n100,u.x),mix(n010,n110,u.x),u.y),
        mix(mix(n001,n101,u.x),mix(n011,n111,u.x),u.y),u.z),-1.0,1.0);
}

float fieldUnsignedNoise(vec2 p, uint salt) { return 0.5 + 0.5 * fieldNoise2(p,salt); }

float fieldVoronoi(vec2 p)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    float best = 2.0;
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 q = ivec2(x,z);
        uint h = fieldHash2(cell+q,0xb5297a4du);
        vec2 jitter = vec2(h & 65535u, h >> 16u) * (1.0 / 65536.0);
        vec2 delta = vec2(q) + jitter - f;
        best = min(best,dot(delta,delta));
    }
    return clamp(sqrt(best),0.0,1.0);
}

float fieldTerraces(float value, float levels, float softness)
{
    levels = max(levels,0.25);
    float v = value * levels;
    float width = clamp(softness,0.01,1.0);
    return (floor(v) + smoothstep(0.5-0.5*width,0.5+0.5*width,fract(v))) / levels;
}

// The rows are temperature, the columns are humidity. IDs:
// rock, dry desert, temperate forest, wet lowlands, ravine, cold uplands.
const uint FIELD_BIOME_2D[25] = uint[25](
    5u,5u,5u,5u,5u,
    0u,0u,2u,2u,4u,
    0u,1u,2u,3u,4u,
    0u,1u,2u,3u,4u,
    0u,1u,1u,3u,4u
);

// Five altitude slices, indexed humidity + temperature*5 + altitude*25.
const uint FIELD_BIOME_3D[125] = uint[125](
    0u,0u,3u,3u,4u, 0u,1u,3u,3u,4u, 0u,1u,2u,3u,4u, 0u,1u,2u,3u,4u, 0u,1u,1u,3u,4u,
    5u,5u,5u,5u,4u, 0u,0u,2u,2u,4u, 0u,1u,2u,3u,4u, 0u,1u,2u,3u,4u, 0u,1u,1u,3u,4u,
    5u,5u,5u,5u,5u, 0u,0u,2u,2u,4u, 0u,1u,2u,3u,4u, 0u,1u,2u,3u,4u, 0u,1u,1u,3u,4u,
    5u,5u,5u,5u,5u, 5u,0u,0u,2u,4u, 0u,0u,2u,2u,4u, 0u,0u,0u,3u,4u, 0u,0u,0u,3u,4u,
    5u,5u,5u,5u,5u, 5u,5u,5u,5u,5u, 0u,0u,5u,5u,5u, 0u,0u,0u,5u,5u, 0u,0u,0u,0u,5u
);

vec2 fieldBiomeRelief(uint biome, vec2 p)
{
    if (biome == 0u) // Legacy desertRoche: clipped plateaus and low-frequency relief.
    {
        float h = clamp((fieldUnsignedNoise(p*0.01,11u)-0.5)*5.0+0.5,0.0,1.0)*20.0;
        h += fieldUnsignedNoise(p*0.0016,12u) + 2.0*fieldUnsignedNoise(p*0.0003,13u);
        return vec2(h*5.0,0.0);
    }
    if (biome == 1u) // Legacy desertSec: cellular rock masses and smaller variation.
    {
        float h = fieldVoronoi(p*0.008) + fieldUnsignedNoise(p*0.016,21u)*0.25;
        h += fieldUnsignedNoise(p*0.003,22u)*0.5;
        return vec2(h*35.0,0.0);
    }
    if (biome == 2u) // Legacy foretTempere: mixed noise, soft terraces, wide 3D band.
    {
        float shape = clamp((fieldVoronoi(p*0.0025)-0.2)*2.5+0.2,0.0,1.0);
        float h = mix(fieldUnsignedNoise(p*0.008,31u)*5.5,fieldUnsignedNoise(p*0.0025,32u),shape);
        float soft = clamp((fieldUnsignedNoise(p*0.004,33u)-0.5)*1.5+0.5,0.5,1.0);
        return vec2(fieldTerraces(h,5.0*fieldUnsignedNoise(p*0.00025,34u),soft)*35.0,200.0);
    }
    if (biome == 3u)
    {
        return vec2(fieldNoise2(p*0.0025,41u)*18.0 + fieldNoise2(p*0.012,42u)*3.0,120.0);
    }
    if (biome == 4u) // Legacy ravin.
    {
        float h = clamp((fieldUnsignedNoise(p*0.005,51u)-0.5)*5.0+0.5,0.0,1.0)*20.0;
        h += fieldUnsignedNoise(p*0.0016,52u) + fieldUnsignedNoise(p*0.0003,53u)*2.0;
        return vec2(-50.0*h,0.0);
    }
    float ridge = 1.0-abs(fieldNoise2(p*0.003,61u));
    return vec2(ridge*ridge*120.0+fieldNoise2(p*0.012,62u)*12.0,60.0);
}

vec2 fieldBlendRelief(vec2 p, vec2 climate)
{
    vec2 cell = clamp(climate,0.0,1.0)*4.0;
    uvec2 i = uvec2(floor(cell));
    uvec2 j = min(i+uvec2(1),uvec2(4));
    vec2 f = fract(cell);
    float weights[6];
    for (uint k=0u;k<6u;++k) weights[k]=0.0;
    weights[FIELD_BIOME_2D[i.x+i.y*5u]] += (1.0-f.x)*(1.0-f.y);
    weights[FIELD_BIOME_2D[j.x+i.y*5u]] += f.x*(1.0-f.y);
    weights[FIELD_BIOME_2D[i.x+j.y*5u]] += (1.0-f.x)*f.y;
    weights[FIELD_BIOME_2D[j.x+j.y*5u]] += f.x*f.y;
    vec2 result=vec2(0);
    for(uint k=0u;k<6u;++k)
        if(weights[k]>0.0) result += fieldBiomeRelief(k,p)*weights[k];
    return clamp(result,vec2(-1150.0,0.0),vec2(240.0,200.0));
}

vec3 fieldBiomeVolume(uint biome)
{
    if(biome==0u) return vec3(1.0,0.0,0.8);
    if(biome==1u) return vec3(0.0,0.0,1.0);
    if(biome==2u) return vec3(0.0,1.0,0.8);
    if(biome==3u) return vec3(0.35,0.65,0.75);
    if(biome==4u) return vec3(0.65,0.35,0.7);
    return vec3(0.4,0.6,0.85);
}

vec3 fieldBlendVolume(vec3 biomeCoordinates)
{
    vec3 cell=clamp(biomeCoordinates,0.0,1.0)*4.0;
    uvec3 i=uvec3(floor(cell));
    uvec3 j=min(i+uvec3(1),uvec3(4));
    vec3 f=fract(cell), result=vec3(0);
    for(uint z=0u;z<2u;++z)
    for(uint y=0u;y<2u;++y)
    for(uint x=0u;x<2u;++x)
    {
        float weight=(x==0u?1.0-f.x:f.x)*(y==0u?1.0-f.y:f.y)*(z==0u?1.0-f.z:f.z);
        if(weight==0.0) continue;
        uint index=(x==0u?i.x:j.x)+(y==0u?i.y:j.y)*5u+(z==0u?i.z:j.z)*25u;
        result += fieldBiomeVolume(FIELD_BIOME_3D[index])*weight;
    }
    return result;
}

FieldColumn fieldColumnAt(vec2 p)
{
    FieldColumn column;
    if(uFieldMode!=0u)
    {
        column.terrain=vec4(uBaseHeight,0.5,0.5,0.0);
        column.volume=vec4(0.0,0.0,0.0,1.0);
        return column;
    }
    float humidity=fieldUnsignedNoise(p*uClimateFrequency,0x243f6a88u);
    float temperature=fieldUnsignedNoise(p*uClimateFrequency,0x85a308d3u);
    vec2 relief=vec2(0.0);
    if(uDetailScale>0.0) relief=fieldBlendRelief(p,vec2(humidity,temperature));
    float height=uBaseHeight+relief.x*uDetailScale;
    if(uHeightAmplitude>0.0)
        height += fieldNoise2(p*uHeightFrequency,0x13198a2eu)*uHeightAmplitude;
    float thickness=uBandThickness+relief.y*uDetailScale;
    if(uBandVariation>0.0)
        thickness += uBandVariation*fieldUnsignedNoise(p*0.0001,0x03707344u);
    thickness=max(0.0,thickness);
    float offset=uBandOffset;
    if(uBandOffsetVariation>0.0)
        offset += uBandOffsetVariation*fieldNoise2(p*0.0004,0xa4093822u);
    float altitude=0.5+(height-uBaseHeight)/(2.0*max(uHeightAmplitude+1150.0*uDetailScale,1.0));
    vec3 volume=fieldBlendVolume(vec3(humidity,temperature,altitude));
    column.terrain=vec4(height,humidity,temperature,thickness);
    column.volume=vec4(offset,volume);
    return column;
}

float fieldDensityAt(vec3 p, FieldColumn column)
{
    if(uFieldMode==1u) return uBaseHeight-p.y;
    if(uFieldMode==2u) return uSphereRadius-length(p-uSphereCenter);

    float halfThickness=column.terrain.w*0.5;
    float base=column.terrain.x-p.y;
    if(halfThickness<=0.0001) return base;
    float center=column.terrain.x+column.volume.x;
    float edgeDistance=halfThickness-abs(p.y-center);
    float baseDensity=clamp(base/halfThickness,-1.0,1.0);
    // The height surface remains present outside the displaced band. A buried
    // band can carve caves without moving the original terrain surface down.
    if(edgeDistance<=0.0 || column.volume.y+column.volume.z<=0.0) return baseDensity;

    float noise=0.0;
    if(column.volume.y>0.0) noise += fieldNoise3(p*0.025,0x299f31d0u)*column.volume.y;
    if(column.volume.z>0.0) noise += fieldNoise3(p*0.01,0x082efa98u)*column.volume.z;
    float influence=smoothstep(0.0,halfThickness*column.volume.w,edgeDistance);
    return mix(baseDensity,noise,influence);
}

vec3 fieldWorldPosition(ivec3 sampleIndex)
{
    // Do integer arithmetic before the only float conversion. All LODs agree
    // exactly at common samples, including negative origins and halo points.
    ivec3 lattice=uOrigin+(sampleIndex-ivec3(1))*int(1u<<uLod);
    return vec3(lattice)*uBaseSpacing;
}

#endif
