#ifndef ENDURANCE_SKY_PALETTE_GLSL
#define ENDURANCE_SKY_PALETTE_GLSL
#include "../../../../Asset/Shader/ProceduralTerrain/field_climate.glsl"

// Game-owned data. The engine never imports this table or knows biome IDs.
struct EnduranceSkyStyle
{
    vec4 horizon;       // RGB, middle elevation
    vec4 middle;        // RGB, gradient power
    vec4 zenith;        // RGB, artistic strength
    vec4 fogMedium;     // scattering coefficient RGB, extinction per metre
    vec4 fogShape;      // base altitude, scale height, noise amount, frequency
    vec4 controls;      // HDR exposure, reserved
};
layout(std430,binding=12) readonly buffer EnduranceSkyStyles
{
    EnduranceSkyStyle enduranceStyles[26];
};
#endif
