#version 450 core
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"
#include "chain.glsl"

vec4 fieldRelief(vec2 p)
{
    // Future spatial mask: replace 1.0 with a function of world XZ p.
    // 0 = no mountains, 1 = full chains; placement remains identical.
    float mountainPresence = 1.0;
    mountainPresence = clamp(mountainPresence, 0.0, 1.0);
    if (mountainPresence <= 0.0) return vec4(0.0);
    float h = frozenChainHeight(p) * mountainPresence / 0.35;
    return vec4(h, 0.0, 0.0, 0.0);
}

#include "../../field_surface_accumulate.glsl"
