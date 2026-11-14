// Test-only point sampling of ONE selected production biome. The two recipe
// bodies are extracted from that biome's actual compute entrypoints. These
// controls describe the actual column and conservative envelope; no recipe
// constants or biome parameters are mixed.
// Full column preparation, weights and accumulation are tested independently
// with BatchFieldRunner and the production kernels.

#include "../../Asset/Shader/Noise/climate_2d.glsl"

FieldColumn fieldProbeColumn(vec2 p)
{
    vec4 relief=fieldClampRelief(fieldRelief(p));
    float macroHeight=uBaseHeight;
    if(uHeightAmplitude>0.0)
        macroHeight+=noiseSimplexFbm2D(p*uHeightFrequency, uSeed ^ (0x13198a2eu), 5, 2.0, 0.5)*uHeightAmplitude;
    float offset=uBandOffset;
    if(uBandOffsetVariation>0.0)
        offset+=uBandOffsetVariation*noisePerlin2D(p*0.0004, uSeed ^ (0xa4093822u));
    vec2 climate=noiseClimate2D(p,uSeed);
    float coast=clamp((macroHeight-uSeaLevel)/max(uSeaBlendWidth,0.001)+0.5,0.0,4.0);
    uvec3 cell=min(uvec3(floor(vec3(climate*4.0,coast))),uvec3(3u));
    uint index=cell.x+4u*cell.y+16u*cell.z;
    uvec4 mapping=fieldBiomeCells[index];
    if(FIELD_BIOME_ID<FIELD_MARINE_BIOME)
        climate=vec2(FIELD_BIOME_ID%5u,FIELD_BIOME_ID/5u)*0.25;
    FieldColumn column;
    precise float height=macroHeight+relief.x*uDetailScale;
    column.terrain=vec4(height,climate,relief.y*uDetailScale);
    column.blend=vec4(noiseFadeQuintic(coast-float(cell.z)),offset,relief.zw);
    column.detail=uvec4(mapping.x|(index<<20u),fieldRepeatedVolumeID(FIELD_BIOME_ID),1u<<FIELD_BIOME_ID);
    // The production pipeline quantizes only after FP32 accumulation. A forced
    // biome has one contribution, so reproduce that single storage round-trip.
    column=fieldUnpackColumn(fieldPackColumn(column,relief.y));
    column.detail.w=1u<<FIELD_BIOME_ID;
    return column;
}

// Test metadata: center offset, conservative low/high Y and 2D activity.
// These controls remain meaningful for arbitrary per-biome SDF/density code.
vec4 fieldProbeVolume(FieldColumn column)
{
    vec2 envelope=fieldVolumeEnvelope(column);
    float centerOffset=column.blend.y+clamp(column.blend.z,-160.0,160.0)*uDetailScale;
    return vec4(centerOffset,envelope,column.blend.w);
}

float fieldProbeDensity(vec3 p,FieldColumn column)
{
    precise float value=column.terrain.x-p.y;
    // Match field_volume_accumulate: a zero band and values outside the
    // conservative envelope never execute a biome volume function.
    vec2 envelope=fieldVolumeEnvelope(column);
    if(column.terrain.w>0.0002 && p.y>=envelope.x && p.y<=envelope.y)
        value+=fieldVolume(p,column);
    return value;
}
