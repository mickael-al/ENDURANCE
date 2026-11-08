// Biome 17: Savanna. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_Savanna
#ifndef ENDURANCE_VOLUME_SAVANNA_GLSL
#define ENDURANCE_VOLUME_SAVANNA_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_Savanna = vec4(-40,8,105,.45);
const vec4 FIELD_SHAPE_Savanna = vec4(.55,.15,.30,.12);
const vec4 FIELD_STYLE_Savanna = vec4(.80,.20,.30,.15);

float fieldVolumeSavanna(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_Savanna,
                                 FIELD_SHAPE_Savanna,FIELD_STYLE_Savanna);
}

#endif
#endif
