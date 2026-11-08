// Biome 0: PolarIce. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_PolarIce
#ifndef ENDURANCE_VOLUME_POLARICE_GLSL
#define ENDURANCE_VOLUME_POLARICE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_PolarIce = vec4(-20,30,72,0.4);
const vec4 FIELD_SHAPE_PolarIce = vec4(.35,.50,.15,.10);
const vec4 FIELD_STYLE_PolarIce = vec4(.85,.70,.15,.30);

float fieldVolumePolarIce(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_PolarIce,
                                 FIELD_SHAPE_PolarIce,FIELD_STYLE_PolarIce);
}

#endif
#endif
