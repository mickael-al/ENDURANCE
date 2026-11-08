// Biome 10: DryPlain. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_DryPlain
#ifndef ENDURANCE_VOLUME_DRYPLAIN_GLSL
#define ENDURANCE_VOLUME_DRYPLAIN_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_DryPlain = vec4(-56,2,48,.70);
const vec4 FIELD_SHAPE_DryPlain = vec4(.75,.10,.15,.07);
const vec4 FIELD_STYLE_DryPlain = vec4(1.00,.25,.10,.10);

float fieldVolumeDryPlain(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_DryPlain,
                                 FIELD_SHAPE_DryPlain,FIELD_STYLE_DryPlain);
}

#endif
#endif
