// Biome 18: DryJungle. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_DryJungle
#ifndef ENDURANCE_VOLUME_DRYJUNGLE_GLSL
#define ENDURANCE_VOLUME_DRYJUNGLE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_DryJungle = vec4(-42,12,148,.35);
const vec4 FIELD_SHAPE_DryJungle = vec4(.25,.15,.60,.17);
const vec4 FIELD_STYLE_DryJungle = vec4(.65,.55,.40,.50);

float fieldVolumeDryJungle(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_DryJungle,
                                 FIELD_SHAPE_DryJungle,FIELD_STYLE_DryJungle);
}

#endif
#endif
