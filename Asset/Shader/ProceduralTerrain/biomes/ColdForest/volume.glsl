// Biome 8: ColdForest. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_ColdForest
#ifndef ENDURANCE_VOLUME_COLDFOREST_GLSL
#define ENDURANCE_VOLUME_COLDFOREST_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_ColdForest = vec4(-42,9,116,.40);
const vec4 FIELD_SHAPE_ColdForest = vec4(.30,.40,.30,.15);
const vec4 FIELD_STYLE_ColdForest = vec4(.70,.20,.45,.40);

float fieldVolumeColdForest(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_ColdForest,
                                 FIELD_SHAPE_ColdForest,FIELD_STYLE_ColdForest);
}

#endif
#endif
