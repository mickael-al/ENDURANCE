// Biome 7: BorealForest. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_BorealForest
#ifndef ENDURANCE_VOLUME_BOREALFOREST_GLSL
#define ENDURANCE_VOLUME_BOREALFOREST_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_BorealForest = vec4(-36,7,92,.40);
const vec4 FIELD_SHAPE_BorealForest = vec4(.45,.15,.40,.13);
const vec4 FIELD_STYLE_BorealForest = vec4(.80,.25,.35,.15);

float fieldVolumeBorealForest(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_BorealForest,
                                 FIELD_SHAPE_BorealForest,FIELD_STYLE_BorealForest);
}

#endif
#endif
