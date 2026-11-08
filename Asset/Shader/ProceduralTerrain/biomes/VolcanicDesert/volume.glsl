// Biome 20: VolcanicDesert. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_VolcanicDesert
#ifndef ENDURANCE_VOLUME_VOLCANICDESERT_GLSL
#define ENDURANCE_VOLUME_VOLCANICDESERT_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_VolcanicDesert = vec4(-68,18,180,.30);
const vec4 FIELD_SHAPE_VolcanicDesert = vec4(.30,.45,.25,.15);
const vec4 FIELD_STYLE_VolcanicDesert = vec4(.95,.75,.65,.75);

float fieldVolumeVolcanicDesert(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_VolcanicDesert,
                                 FIELD_SHAPE_VolcanicDesert,FIELD_STYLE_VolcanicDesert);
}

#endif
#endif
