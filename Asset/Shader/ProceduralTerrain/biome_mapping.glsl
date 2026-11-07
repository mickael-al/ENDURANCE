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

// Column-only activity checks should not evaluate a mask noise merely because
// a zero-weight corner names that biome (for example polar ice above ocean).
float fieldMappedVolumeWeight(uvec2 encoded,vec3 ratios,uint biome)
{
    uvec4 lo=uvec4(encoded.x,encoded.x>>5u,encoded.x>>10u,encoded.x>>15u)&31u;
    uvec4 hi=uvec4(encoded.x>>20u,encoded.x>>25u,encoded.y,encoded.y>>5u)&31u;
    vec4 quad=fieldQuadWeights(ratios.xy);
    return dot(vec4(equal(lo,uvec4(biome))),quad)*(1.0-ratios.z)+
           dot(vec4(equal(hi,uvec4(biome))),quad)*ratios.z;
}
#endif
