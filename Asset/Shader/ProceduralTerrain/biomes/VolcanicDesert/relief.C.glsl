#version 450 core
// Biome 20: fractured basalt plains and irregular, open-crater volcanoes.
#define FIELD_BIOME_ID 20u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "volcanoes.glsl"
#include "ground.glsl"

vec4 fieldRelief(vec2 p)
{
    float mountain = volcanicMountainHeight(p);
    // Plate fractures dominate the plains; summit detail follows the flanks.
    float ground = volcanicGroundHeight(p) * mix(1.0, 0.15, smoothstep(0.0, 180.0, mountain));
    return vec4((ground + mountain) / VOLCANIC_REFERENCE_SCALE, 0.0, 0.0, 0.0);
}

#include "../../field_surface_accumulate.glsl"
