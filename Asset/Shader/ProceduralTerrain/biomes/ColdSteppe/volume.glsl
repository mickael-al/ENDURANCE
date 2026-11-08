// Biome 6: ColdSteppe. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_ColdSteppe
#ifndef ENDURANCE_VOLUME_COLDSTEPPE_GLSL
#define ENDURANCE_VOLUME_COLDSTEPPE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_ColdSteppe = vec4(-38,3,56,.60);
const vec4 FIELD_SHAPE_ColdSteppe = vec4(.70,.10,.20,.08);
const vec4 FIELD_STYLE_ColdSteppe = vec4(.95,.20,.15,.10);

float fieldVolumeColdSteppe(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_ColdSteppe,
                                 FIELD_SHAPE_ColdSteppe,FIELD_STYLE_ColdSteppe);
}

#endif
#endif
