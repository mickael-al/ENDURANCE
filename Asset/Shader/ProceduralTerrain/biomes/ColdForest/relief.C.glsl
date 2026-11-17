#version 450 core
// Biome 8: regional river excavation and rare porous spiral sinkholes.
#define FIELD_BIOME_ID 8u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "pits.glsl"

vec4 fieldRelief(vec2 p)
{
    float control = coldForestPitControl(p);
    return vec4(coldForestGroundHeight(p), coldForestPitBand(p, control), 0.0, control);
}

#include "../../field_surface_accumulate.glsl"
