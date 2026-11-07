// Deterministic world field. Positive density is solid, negative density is air.
// The field is not a signed distance. Its definition never depends on LOD.
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
uniform float uSeaLevel;
uniform float uSeaBlendWidth;
uniform int uBiomeOverride;
uniform vec3 uSphereCenter;
uniform float uSphereRadius;

// Four vec4s, 64 bytes/std430. Shared by four vertical invocations in the density
// pass. All biome lookup, blending and 2D work is done once per XZ column.
struct FieldColumn
{
    vec4 terrain; // final height, humidity, temperature, total volume band width
    vec4 volume;  // band offset, displacement metres, carving scale metres, feather
    vec4 shape;   // three fixed noise weights, cave radius
    vec4 style;   // tubes vs sheets, angular cross-section, roughness, strata
};

#include "field_noise.glsl"
#include "biome_matrix.glsl"
#include "biome_relief.glsl"
#include "biome_volume.glsl"

vec4 fieldBlendRelief(vec2 p, vec2 climate)
{
    vec2 cell=clamp(climate,0.0,1.0)*4.0;
    uvec2 i=uvec2(floor(cell));
    uvec2 j=min(i+uvec2(1),uvec2(4));
    vec2 f=fieldFade(fract(cell));
    float weights[26];
    for(uint k=0u;k<FIELD_BIOME_COUNT;++k) weights[k]=0.0;
    weights[FIELD_BIOME_2D[i.x+i.y*5u]]+=(1.0-f.x)*(1.0-f.y);
    weights[FIELD_BIOME_2D[j.x+i.y*5u]]+=f.x*(1.0-f.y);
    weights[FIELD_BIOME_2D[i.x+j.y*5u]]+=(1.0-f.x)*f.y;
    weights[FIELD_BIOME_2D[j.x+j.y*5u]]+=f.x*f.y;
    vec4 result=vec4(0.0);
    for(uint k=0u;k<FIELD_BIOME_COUNT;++k)
        if(weights[k]>0.0) result+=fieldBiomeRelief(k,p)*weights[k];
    return clamp(result,vec4(-640.0,0.0,-160.0,0.0),vec4(640.0,320.0,160.0,1.0));
}

BiomeVolume fieldBlendVolume(vec3 biomeCoordinates)
{
    vec3 cell=clamp(biomeCoordinates,0.0,1.0)*4.0;
    uvec3 i=uvec3(floor(cell));
    uvec3 j=min(i+uvec3(1),uvec3(4));
    vec3 f=fieldFade(fract(cell));
    float weights[26];
    for(uint k=0u;k<FIELD_BIOME_COUNT;++k) weights[k]=0.0;
    for(uint z=0u;z<2u;++z)
    for(uint y=0u;y<2u;++y)
    for(uint x=0u;x<2u;++x)
    {
        float weight=(x==0u?1.0-f.x:f.x)*(y==0u?1.0-f.y:f.y)*(z==0u?1.0-f.z:f.z);
        uint index=(x==0u?i.x:j.x)+(y==0u?i.y:j.y)*5u+(z==0u?i.z:j.z)*25u;
        weights[FIELD_BIOME_3D[index]]+=weight;
    }
    BiomeVolume result;
    result.volume=vec4(0.0); result.shape=vec4(0.0); result.style=vec4(0.0);
    for(uint k=0u;k<FIELD_BIOME_COUNT;++k)
    {
        if(weights[k]<=0.0) continue;
        BiomeVolume recipe=fieldBiomeVolume(k);
        result.volume+=recipe.volume*weights[k];
        result.shape+=recipe.shape*weights[k];
        result.style+=recipe.style*weights[k];
    }
    return result;
}

vec2 fieldClimateAt(vec2 p)
{
    // A raw 0.5+0.5*Perlin concentrates almost everything in the central cells.
    // This bounded contrast makes all five climate bands reachable, with wide
    // cores and smooth borders rather than 25 weakly different central blends.
    vec2 n=vec2(fieldNoise2(p*uClimateFrequency,0x243f6a88u),
                fieldNoise2(p*uClimateFrequency,0x85a308d3u));
    return smoothstep(vec2(-0.32),vec2(0.32),n);
}

FieldColumn fieldColumnAt(vec2 p)
{
    FieldColumn column;
    column.terrain=vec4(uBaseHeight,0.5,0.5,0.0);
    column.volume=vec4(0.0,0.0,0.0,1.0);
    column.shape=vec4(1.0,0.0,0.0,0.0);
    column.style=vec4(0.0);
    if(uFieldMode!=0u) return column;

    float macroHeight=uBaseHeight;
    if(uHeightAmplitude>0.0)
        macroHeight+=fieldNoise2(p*uHeightFrequency,0x13198a2eu)*uHeightAmplitude;
    vec2 climate=fieldClimateAt(p);
    float coastCoordinate=(macroHeight-uSeaLevel)/max(uSeaBlendWidth,0.001)+0.5;
    float marineWeight=1.0-fieldFade(clamp(coastCoordinate,0.0,1.0));
    vec4 relief=vec4(0.0,0.0,0.0,1.0);
    BiomeVolume recipe;
    if(uBiomeOverride>=0 && uBiomeOverride<int(FIELD_BIOME_COUNT))
    {
        uint biome=uint(uBiomeOverride);
        if(uDetailScale>0.0) relief=fieldBiomeRelief(biome,p);
        recipe=fieldBiomeVolume(biome);
        if(biome<FIELD_MARINE_BIOME)
            climate=vec2(biome%5u,biome/5u)*0.25;
    }
    else
    {
        if(uDetailScale>0.0)
        {
            if(marineWeight<1.0) relief=fieldBlendRelief(p,climate);
            if(marineWeight>0.0)
                relief=mix(relief,fieldBiomeRelief(FIELD_MARINE_BIOME,p),marineWeight);
        }
        // The first altitude interval is the shoreline. Higher intervals keep
        // their paired land recipes; the explicit matrix can be edited later.
        recipe=fieldBlendVolume(vec3(climate,clamp(coastCoordinate,0.0,4.0)*0.25));
    }
    relief=clamp(relief,vec4(-640.0,0.0,-160.0,0.0),vec4(640.0,320.0,160.0,1.0));
    float height=macroHeight+relief.x*uDetailScale;
    float thickness=uBandThickness+relief.y*uDetailScale;
    if(uBandVariation>0.0)
        thickness+=uBandVariation*fieldUnsignedNoise(p*0.0001,0x03707344u);
    float offset=uBandOffset+clamp(relief.z+recipe.volume.x,-160.0,160.0)*uDetailScale;
    if(uBandOffsetVariation>0.0)
        offset+=uBandOffsetVariation*fieldNoise2(p*0.0004,0xa4093822u);

    column.terrain=vec4(height,climate,max(0.0,thickness));
    column.volume=vec4(offset,recipe.volume.yz*uDetailScale,recipe.volume.w);
    // Surface landmark masks can strengthen recesses on rock masses while
    // keeping the running surface quieter. No extra per-voxel buffer fetch.
    column.volume.y*=mix(0.55,1.0,relief.w);
    column.volume.z*=mix(0.85,1.0,relief.w);
    column.shape=recipe.shape;
    column.style=recipe.style;
    return column;
}

float fieldDensityAt(vec3 p, FieldColumn column)
{
    if(uFieldMode==1u) return uBaseHeight-p.y;
    if(uFieldMode==2u) return uSphereRadius-length(p-uSphereCenter);

    float base=column.terrain.x-p.y;
    float halfThickness=column.terrain.w*0.5;
    if(halfThickness<=0.0001 || column.volume.y+column.volume.z<=0.0) return base;
    float edgeDistance=halfThickness-abs(p.y-(column.terrain.x+column.volume.x));
    if(edgeDistance<=0.0) return base;
    float influence=smoothstep(0.0,max(0.0001,halfThickness*column.volume.w),edgeDistance);

    // Three world-anchored fields, regardless of the number of blended biomes.
    // Different anisotropy distinguishes rounded chambers, bedding planes and
    // tall joints. No trigonometric hash, repeated biome evaluation or 3D
    // Voronoi's 27 feature-point search is needed in the volume pass.
    float n0=fieldNoise3(p*0.0125,0x299f31d0u);
    float n1=fieldNoise3(p*vec3(0.00625,0.020,0.00625),0x082efa98u);
    float n2=fieldNoise3(p*vec3(0.018,0.005,0.018),0xec4e6c89u);
    float a=dot(column.shape.xyz,vec3(n0,n1,n2));
    float displacement=mix(a,0.6*a+0.4*n0,column.style.z)*column.volume.y;
    float density=base+influence*displacement;
    if(column.volume.z<=0.0) return density;

    float b=0.75*n1-0.65*n2;
    vec2 d=abs(vec2(a,b));
    float roundTube=length(d);
    float diamondTube=(d.x+d.y)*0.75;
    float squareTube=max(d.x,d.y);
    float tube=mix(roundTube,diamondTube,clamp(column.style.y*2.0,0.0,1.0));
    tube=mix(tube,squareTube,clamp(column.style.y*2.0-1.0,0.0,1.0));
    float cavity=mix(abs(a),tube,column.style.x);
    // Continuous triangle-wave strata: flat ledges without a discontinuous
    // density step. Their phase remains fixed in world space across all LODs.
    float layer=abs(2.0*fract(p.y*0.025+0.08*n0)-1.0);
    float layerWidth=mix(0.55,1.35,smoothstep(0.2,0.8,layer));
    float radius=column.shape.w*mix(1.0,layerWidth,column.style.w);
    float cavityDensity=(cavity-radius)*column.volume.z;
    // Subtract cavities from the existing height terrain. In particular, an
    // offset buried band cannot replace the entire solid below H by raw noise.
    return mix(density,min(density,cavityDensity),influence);
}

vec3 fieldWorldPosition(ivec3 sampleIndex)
{
    ivec3 lattice=uOrigin+(sampleIndex-ivec3(1))*int(1u<<uLod);
    return vec3(lattice)*uBaseSpacing;
}

#endif
