// Biome 11: Meadow. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_Meadow
#ifndef ENDURANCE_VOLUME_MEADOW_GLSL
#define ENDURANCE_VOLUME_MEADOW_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_Meadow = vec4(-50,3,55,.65);
const vec4 FIELD_SHAPE_Meadow = vec4(.70,.10,.20,.075);
const vec4 FIELD_STYLE_Meadow = vec4(.95,.05,.15,.05);

float fieldVolumeMeadow(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_Meadow,
                                 FIELD_SHAPE_Meadow,FIELD_STYLE_Meadow);
}

#endif
#endif
