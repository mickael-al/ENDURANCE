// Biome 16: DrySavanna. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_DrySavanna
#ifndef ENDURANCE_VOLUME_DRYSAVANNA_GLSL
#define ENDURANCE_VOLUME_DRYSAVANNA_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_DrySavanna = vec4(-42,6,95,.50);
const vec4 FIELD_SHAPE_DrySavanna = vec4(.60,.10,.30,.11);
const vec4 FIELD_STYLE_DrySavanna = vec4(.85,.40,.25,.25);

float fieldVolumeDrySavanna(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_DrySavanna,
                                 FIELD_SHAPE_DrySavanna,FIELD_STYLE_DrySavanna);
}

#endif
#endif
