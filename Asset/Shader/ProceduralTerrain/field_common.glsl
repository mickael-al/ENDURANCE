// Shared field uniforms, column layout and lattice coordinates; positive is solid.
#ifndef ENDURANCE_FIELD_COMMON_GLSL
#define ENDURANCE_FIELD_COMMON_GLSL

uniform uint uCells;
#ifdef FIELD_BATCHED
// Each invocation selects a GPU job before evaluating any world-space noise.
// The same recipes also work in diagnostic shaders using ordinary uniforms.
ivec3 uOrigin;
uint uLod;
uint uSeed;
#else
uniform ivec3 uOrigin;
uniform uint uLod;
uniform uint uSeed;
#endif
uniform float uBaseSpacing;
uniform uint uFieldMode;
uniform float uBaseHeight;
uniform float uHeightAmplitude;
uniform float uHeightFrequency;
uniform float uClimateFrequency;
uniform float uBandOffset;
uniform float uBandOffsetVariation;
uniform float uDetailScale;
uniform float uSeaLevel;
uniform float uSeaBlendWidth;
uniform int uBiomeOverride;
uniform vec3 uSphereCenter;
uniform float uSphereRadius;

// Decoded recipe interface and FP32 construction scratch, 48 bytes/std430.
// Persistent columns use PackedFieldColumn (32 bytes); quantization is performed
// only after all 2D contributions, never after each biome addition.
struct FieldColumn
{
    vec4 terrain; // surface height, humidity, temperature, total volume band
    vec4 blend;   // altitude ratio, global offset, relief offset, biome-owned control
    uvec4 detail; // surface IDs + cell, volume IDs 0..5, IDs 6..7;
                  // .w holds macro-height bits in scratch and is reserved after decode
};

#include "field_noise_common.glsl"
#include "biome_mapping.glsl"
#include "field_storage.glsl"

PackedFieldColumn fieldPackColumn(FieldColumn column,float unscaledThickness)
{
    PackedFieldColumn stored;
    stored.height=column.terrain.x;
    stored.humidity=column.terrain.y;
    stored.temperature=column.terrain.z;
    stored.altitudeRatio=column.blend.x;
    stored.globalOffset=column.blend.y;
    stored.controls=fieldPackControls(unscaledThickness,column.blend.z);
    uint control=uint(round(clamp(column.blend.w,0.0,1.0)*65535.0));
    uint cell=(column.detail.x>>20u)&63u;
    stored.detail=uvec2((column.detail.y&0x3fffffffu)|((column.detail.z&3u)<<30u),
        ((column.detail.z>>2u)&255u)|(control<<8u)|(cell<<24u)
        |(uint(column.terrain.w>0.0002)<<30u));
    return stored;
}

FieldColumn fieldUnpackColumn(PackedFieldColumn stored)
{
    FieldColumn column;
    column.terrain=vec4(stored.height,stored.humidity,stored.temperature,
                        fieldStoredThickness(stored,uDetailScale));
    column.blend=vec4(stored.altitudeRatio,stored.globalOffset,
                     fieldStoredOffset(stored),fieldStoredControl(stored));
    uint cell=(stored.detail.y>>24u)&63u;
    column.detail=uvec4(fieldBiomeCells[cell].x|(cell<<20u),
        stored.detail.x&0x3fffffffu,
        (stored.detail.x>>30u)|((stored.detail.y&255u)<<2u),0u);
    return column;
}

vec3 fieldWorldPosition(ivec3 sampleIndex)
{
    ivec3 lattice=uOrigin+(sampleIndex-ivec3(1))*int(1u<<uLod);
    return vec3(lattice)*uBaseSpacing;
}

#endif
