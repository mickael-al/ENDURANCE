#version 450 core
// Biome 3: Taiga. Existing raw relief is scaled by uDetailScale.
// Added pillar dimensions and placement are expressed directly in world metres.
#define FIELD_BIOME_ID 3u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_landforms_2d.glsl"

#include "height.glsl"
#include "pillars.glsl"

vec4 fieldRelief(vec2 p)
{
    vec4 relief = taigaBaseRelief(p);
    relief.y = max(relief.y, taigaPillarBand(p, relief.x));
    return relief;
}

#include "../../field_surface_accumulate.glsl"
