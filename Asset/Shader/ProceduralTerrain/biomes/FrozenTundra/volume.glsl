// Biome 1: FrozenTundra. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_FrozenTundra
#ifndef ENDURANCE_VOLUME_FROZENTUNDRA_GLSL
#define ENDURANCE_VOLUME_FROZENTUNDRA_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_FrozenTundra = vec4(-20,3,72,.40);
const vec4 FIELD_SHAPE_FrozenTundra = vec4(.35,.50,.15,.10);
const vec4 FIELD_STYLE_FrozenTundra = vec4(.85,.70,.15,.30);

float fieldVolumeFrozenTundra(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_FrozenTundra,
                                 FIELD_SHAPE_FrozenTundra,FIELD_STYLE_FrozenTundra);
}

#endif
#endif
