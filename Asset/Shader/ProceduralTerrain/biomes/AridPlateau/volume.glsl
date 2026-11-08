// Biome 22: AridPlateau. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_AridPlateau
#ifndef ENDURANCE_VOLUME_ARIDPLATEAU_GLSL
#define ENDURANCE_VOLUME_ARIDPLATEAU_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_AridPlateau = vec4(-56,10,168,.30);
const vec4 FIELD_SHAPE_AridPlateau = vec4(.15,.70,.15,.20);
const vec4 FIELD_STYLE_AridPlateau = vec4(.35,.70,.25,1.00);

float fieldVolumeAridPlateau(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_AridPlateau,
                                 FIELD_SHAPE_AridPlateau,FIELD_STYLE_AridPlateau);
}

#endif
#endif
