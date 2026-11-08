// Biome 3: Taiga. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_Taiga
#ifndef ENDURANCE_VOLUME_TAIGA_GLSL
#define ENDURANCE_VOLUME_TAIGA_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_Taiga = vec4(-30,6,90,.40);
const vec4 FIELD_SHAPE_Taiga = vec4(.35,.25,.40,.13);
const vec4 FIELD_STYLE_Taiga = vec4(.80,.20,.30,.10);

float fieldVolumeTaiga(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_Taiga,
                                 FIELD_SHAPE_Taiga,FIELD_STYLE_Taiga);
}

#endif
#endif
