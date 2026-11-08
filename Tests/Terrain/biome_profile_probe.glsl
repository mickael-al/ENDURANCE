// Test-only point sampling of ONE selected production biome. The two recipe
// bodies are extracted from that biome's actual compute entrypoints. These
// controls describe its optional default helper; no biome parameters are mixed.
// Full column preparation, weights and accumulation are tested independently
// with BatchFieldRunner and the production kernels.

FieldColumn fieldProbeColumn(vec2 p)
{
    vec4 relief=fieldClampRelief(fieldRelief(p));
    float macroHeight=uBaseHeight;
    if(uHeightAmplitude>0.0)
        macroHeight+=fieldNoise2(p*uHeightFrequency,0x13198a2eu)*uHeightAmplitude;
    float offset=uBandOffset;
    if(uBandOffsetVariation>0.0)
        offset+=uBandOffsetVariation*fieldNoise2(p*0.0004,0xa4093822u);
    vec2 climate=smoothstep(vec2(-0.32),vec2(0.32),
        vec2(fieldNoise2(p*uClimateFrequency,0x243f6a88u),
             fieldNoise2(p*uClimateFrequency,0x85a308d3u)));
    float coast=clamp((macroHeight-uSeaLevel)/max(uSeaBlendWidth,0.001)+0.5,0.0,4.0);
    uvec3 cell=min(uvec3(floor(vec3(climate*4.0,coast))),uvec3(3u));
    uint index=cell.x+4u*cell.y+16u*cell.z;
    uvec4 mapping=fieldBiomeCells[index];
    if(FIELD_BIOME_ID<FIELD_MARINE_BIOME)
        climate=vec2(FIELD_BIOME_ID%5u,FIELD_BIOME_ID/5u)*0.25;
    FieldColumn column;
    precise float height=macroHeight+relief.x*uDetailScale;
    column.terrain=vec4(height,climate,relief.y*uDetailScale);
    column.blend=vec4(fieldFade(coast-float(cell.z)),offset,relief.zw);
    column.detail=uvec4(mapping.x|(index<<20u),fieldRepeatedVolumeID(FIELD_BIOME_ID),1u<<FIELD_BIOME_ID);
    return column;
}

vec4 fieldProbeVolume(FieldColumn column)
{
    vec4 volume=FIELD_VOLUME;
    volume.x=column.blend.y+clamp(column.blend.z+volume.x,-160.0,160.0)*uDetailScale;
    volume.y*=uDetailScale*mix(0.55,1.0,column.blend.w);
    volume.z*=uDetailScale*mix(0.85,1.0,column.blend.w);
    return volume;
}

float fieldProbeDensity(vec3 p,FieldColumn column)
{
    precise float value=column.terrain.x-p.y+fieldVolume(p,column);
    return value;
}
