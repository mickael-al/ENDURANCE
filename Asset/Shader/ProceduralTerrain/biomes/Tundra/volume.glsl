// Biome 2: Tundra. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_Tundra
#ifndef ENDURANCE_VOLUME_TUNDRA_GLSL
#define ENDURANCE_VOLUME_TUNDRA_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_Tundra = vec4(-30,2,52,.55);
const vec4 FIELD_SHAPE_Tundra = vec4(.65,.15,.20,.075);
const vec4 FIELD_STYLE_Tundra = vec4(.95,.10,.10,.10);

float fieldVolumeTundra(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_Tundra,
                                 FIELD_SHAPE_Tundra,FIELD_STYLE_Tundra);
}

#endif
#endif
