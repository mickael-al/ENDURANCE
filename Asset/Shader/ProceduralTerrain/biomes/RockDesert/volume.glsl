// Biome 15: RockDesert. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_RockDesert
#ifndef ENDURANCE_VOLUME_ROCKDESERT_GLSL
#define ENDURANCE_VOLUME_ROCKDESERT_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_RockDesert = vec4(-44,14,160,.30);
const vec4 FIELD_SHAPE_RockDesert = vec4(.40,.40,.20,.19);
const vec4 FIELD_STYLE_RockDesert = vec4(.45,.65,.35,.80);

float fieldVolumeRockDesert(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_RockDesert,
                                 FIELD_SHAPE_RockDesert,FIELD_STYLE_RockDesert);
}

#endif
#endif
