// Biome 14: TemperateMarsh. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_TemperateMarsh
#ifndef ENDURANCE_VOLUME_TEMPERATEMARSH_GLSL
#define ENDURANCE_VOLUME_TEMPERATEMARSH_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_TemperateMarsh = vec4(-24,3,58,.65);
const vec4 FIELD_SHAPE_TemperateMarsh = vec4(.25,.60,.15,.10);
const vec4 FIELD_STYLE_TemperateMarsh = vec4(.85,.10,.20,.40);

float fieldVolumeTemperateMarsh(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_TemperateMarsh,
                                 FIELD_SHAPE_TemperateMarsh,FIELD_STYLE_TemperateMarsh);
}

#endif
#endif
