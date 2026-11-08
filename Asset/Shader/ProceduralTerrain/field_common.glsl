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

// Three vec4-sized records, 48 bytes/std430 (previously 64).
// All floating-point field controls and interpolation ratios remain float32.
struct FieldColumn
{
    vec4 terrain; // surface height, humidity, temperature, total volume band
    vec4 blend;   // altitude ratio, global offset, relief offset, rock mask
    uvec4 detail; // surface IDs + matrix cell, volume IDs 0..5, IDs 6..7, candidate mask
};

#include "field_noise_common.glsl"
#include "biome_mapping.glsl"

vec3 fieldWorldPosition(ivec3 sampleIndex)
{
    ivec3 lattice=uOrigin+(sampleIndex-ivec3(1))*int(1u<<uLod);
    return vec3(lattice)*uBaseSpacing;
}

#endif
