// Biome 19: TropicalJungle. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_TropicalJungle
#ifndef ENDURANCE_VOLUME_TROPICALJUNGLE_GLSL
#define ENDURANCE_VOLUME_TROPICALJUNGLE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_TropicalJungle = vec4(-52,16,176,.30);
const vec4 FIELD_SHAPE_TropicalJungle = vec4(.20,.15,.65,.20);
const vec4 FIELD_STYLE_TropicalJungle = vec4(.65,.15,.60,.15);

float fieldVolumeTropicalJungle(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_TropicalJungle,
                                 FIELD_SHAPE_TropicalJungle,FIELD_STYLE_TropicalJungle);
}

#endif
#endif
