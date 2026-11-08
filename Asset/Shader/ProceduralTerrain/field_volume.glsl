// Density pass: blend the biome controls locally, then evaluate one common field.
#ifndef ENDURANCE_FIELD_VOLUME_GLSL
#define ENDURANCE_FIELD_VOLUME_GLSL

#include "field_common.glsl"
#include "field_noise_3d.glsl"
#include "biome_volume.glsl"

vec2 fieldColumnClimateRatio(FieldColumn column)
{
    vec2 grid=clamp(column.terrain.yz,0.0,1.0)*4.0;
    return fieldFade(grid-min(floor(grid),vec2(3.0)));
}

// Return raw table controls. Repeated matrix IDs share one table lookup;
// local humidity/temperature/altitude still determine the trilinear weights.
void fieldVolumeParameters(FieldColumn column,vec2 climateRatio,
                           out vec4 volume,out vec4 shape,out vec4 style)
{
    if(uBiomeOverride>=0 && uBiomeOverride<int(FIELD_BIOME_COUNT))
    {
        volume=FIELD_BIOME_VOLUME[uBiomeOverride][0];
        shape=FIELD_BIOME_VOLUME[uBiomeOverride][1];
        style=FIELD_BIOME_VOLUME[uBiomeOverride][2];
        return;
    }
    uvec2 encoded=column.detail.yz;
    uvec4 idsLow=uvec4(encoded.x,encoded.x>>5u,encoded.x>>10u,encoded.x>>15u)&31u;
    uvec4 idsHigh=uvec4(encoded.x>>20u,encoded.x>>25u,encoded.y,encoded.y>>5u)&31u;
    vec4 quad=fieldQuadWeights(climateRatio);
    vec4 low=quad*(1.0-column.blend.x);
    vec4 high=quad*column.blend.x;
    volume=vec4(0.0);
    shape=vec4(0.0);
    style=vec4(0.0);
    uint remaining=column.detail.w;
    while(remaining!=0u)
    {
        uint biome=uint(findLSB(remaining));
        remaining&=remaining-1u;
        float weight=dot(vec4(equal(idsLow,uvec4(biome))),low)
                    +dot(vec4(equal(idsHigh,uvec4(biome))),high);
        if(weight>0.0)
        {
            volume+=FIELD_BIOME_VOLUME[biome][0]*weight;
            shape+=FIELD_BIOME_VOLUME[biome][1]*weight;
            style+=FIELD_BIOME_VOLUME[biome][2]*weight;
        }
    }
}

// Fold all column-only work into the values shared by four Y invocations.
// terrain: surface height, half band thickness.
// volume: absolute band center, scaled displacement, scaled carving, feather distance.
void fieldPrepareVolume(FieldColumn column,out vec2 terrain,
                        out vec4 volume,out vec4 shape,out vec4 style)
{
    terrain=vec2(column.terrain.x,0.5*column.terrain.w);
    volume=vec4(0.0);
    shape=vec4(0.0);
    style=vec4(0.0);
    if(uFieldMode!=0u || terrain.y<=0.0001 || uDetailScale<=0.0) return;
    fieldVolumeParameters(column,fieldColumnClimateRatio(column),volume,shape,style);
    volume.yz*=uDetailScale;
    volume.y*=mix(0.55,1.0,column.blend.w);
    volume.z*=mix(0.85,1.0,column.blend.w);
    float offset=column.blend.y+clamp(column.blend.z+volume.x,-160.0,160.0)*uDetailScale;
    volume.x=column.terrain.x+offset;
    volume.w=max(0.0001,terrain.y*volume.w);
}

float fieldDensityAt(vec3 p,FieldColumn column)
{
    vec2 terrain;
    vec4 volume,shape,style;
    fieldPrepareVolume(column,terrain,volume,shape,style);
    return fieldDensityAtPrepared(p,terrain,volume,shape,style);
}

#endif
