// Biome 25: Marine. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_Marine
#ifndef ENDURANCE_VOLUME_MARINE_GLSL
#define ENDURANCE_VOLUME_MARINE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_Marine = vec4(-44,12,148,.40);
const vec4 FIELD_SHAPE_Marine = vec4(.35,.40,.25,.16);
const vec4 FIELD_STYLE_Marine = vec4(.60,.30,.45,.70);

float fieldVolumeMarine(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_Marine,
                                 FIELD_SHAPE_Marine,FIELD_STYLE_Marine);
}

#endif
#endif
