// Biome 12: TemperateForest. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_TemperateForest
#ifndef ENDURANCE_VOLUME_TEMPERATEFOREST_GLSL
#define ENDURANCE_VOLUME_TEMPERATEFOREST_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_TemperateForest = vec4(-34,9,108,.40);
const vec4 FIELD_SHAPE_TemperateForest = vec4(.35,.20,.45,.14);
const vec4 FIELD_STYLE_TemperateForest = vec4(.80,.20,.40,.25);

float fieldVolumeTemperateForest(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_TemperateForest,
                                 FIELD_SHAPE_TemperateForest,FIELD_STYLE_TemperateForest);
}

#endif
#endif
