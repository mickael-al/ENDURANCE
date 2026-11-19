#version 450 core
// Biome 8: regional rivers and overlapping porous spiral sinkholes.
#define FIELD_BIOME_ID 8u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "pits.glsl"

vec4 fieldRelief(vec2 p)
{
    vec2 pits = coldForestPitFields(p);
    return vec4(coldForestGroundHeight(p), coldForestPitBand(p, pits.x), 0.0, pits.y);
}

#include "../../field_surface_accumulate.glsl"
