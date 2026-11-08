// Biome 21: RedDesert. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_RedDesert
#ifndef ENDURANCE_VOLUME_REDDESERT_GLSL
#define ENDURANCE_VOLUME_REDDESERT_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_RedDesert = vec4(-44,12,156,.30);
const vec4 FIELD_SHAPE_RedDesert = vec4(.30,.50,.20,.18);
const vec4 FIELD_STYLE_RedDesert = vec4(.50,.50,.30,.90);

float fieldVolumeRedDesert(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_RedDesert,
                                 FIELD_SHAPE_RedDesert,FIELD_STYLE_RedDesert);
}

#endif
#endif
