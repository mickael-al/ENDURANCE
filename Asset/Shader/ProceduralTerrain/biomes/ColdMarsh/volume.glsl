// Biome 9: ColdMarsh. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_ColdMarsh
#ifndef ENDURANCE_VOLUME_COLDMARSH_GLSL
#define ENDURANCE_VOLUME_COLDMARSH_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_ColdMarsh = vec4(-20,2,44,.65);
const vec4 FIELD_SHAPE_ColdMarsh = vec4(.20,.65,.15,.08);
const vec4 FIELD_STYLE_ColdMarsh = vec4(.90,.05,.10,.25);

float fieldVolumeColdMarsh(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_ColdMarsh,
                                 FIELD_SHAPE_ColdMarsh,FIELD_STYLE_ColdMarsh);
}

#endif
#endif
