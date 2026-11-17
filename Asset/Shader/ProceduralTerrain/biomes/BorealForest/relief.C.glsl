#version 450 core
// Biome 7: original soil plus four independently thresholded mountain layers.
#define FIELD_BIOME_ID 7u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "height.glsl"

vec4 fieldRelief(vec2 p)
{
    float mountain = borealMountainHeight(p);
    float h = borealGroundHeight(p) + mountain;
    // Only the added mountain height is transmitted to 3D, normalized to [0,1].
    return vec4(h, borealMountainBand(p, mountain), 0.0,
        mountain / BOREAL_MAX_MOUNTAIN_HEIGHT);
}

#include "../../field_surface_accumulate.glsl"
