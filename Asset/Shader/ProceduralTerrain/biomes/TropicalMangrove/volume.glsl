// Biome 24: TropicalMangrove. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_TropicalMangrove
#ifndef ENDURANCE_VOLUME_TROPICALMANGROVE_GLSL
#define ENDURANCE_VOLUME_TROPICALMANGROVE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_TropicalMangrove = vec4(-18,3,52,.70);
const vec4 FIELD_SHAPE_TropicalMangrove = vec4(.15,.75,.10,.10);
const vec4 FIELD_STYLE_TropicalMangrove = vec4(.90,.10,.15,.35);

float fieldVolumeTropicalMangrove(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_TropicalMangrove,
                                 FIELD_SHAPE_TropicalMangrove,FIELD_STYLE_TropicalMangrove);
}

#endif
#endif
