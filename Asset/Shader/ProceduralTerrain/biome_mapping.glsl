#ifndef ENDURANCE_BIOME_MAPPING_GLSL
#define ENDURANCE_BIOME_MAPPING_GLSL

const uint FIELD_BIOME_COUNT=26u;
const uint FIELD_MARINE_BIOME=25u;

// C++ owns the two biome matrices. Only their compact cell corner descriptors
// are uploaded once; no climate map or local ratio travels through the CPU.
// x: four surface IDs; y: first six volume IDs; z: final two volume IDs.
// w: CPU-precomputed candidate bitmask for the eight volume corners.
layout(std430,binding=7) readonly buffer FieldBiomeMapping { uvec4 fieldBiomeCells[64]; };

uvec4 fieldUnpackSurface(uint encoded)
{
    return uvec4(encoded,encoded>>5u,encoded>>10u,encoded>>15u)&31u;
}

vec4 fieldQuadWeights(vec2 f)
{
    vec4 ratios=vec4(1.0-f.x,f.x,1.0-f.x,f.x)*vec4(1.0-f.y,1.0-f.y,f.y,f.y);
    return ratios;
}

uvec2 fieldRepeatedVolumeID(uint biome)
{
    uint low=biome|(biome<<5u)|(biome<<10u)|(biome<<15u)|(biome<<20u)|(biome<<25u);
    return uvec2(low,biome|(biome<<5u));
}

#endif
