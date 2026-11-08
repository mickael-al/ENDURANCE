// Biome 13: TemperateRainforest. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_TemperateRainforest
#ifndef ENDURANCE_VOLUME_TEMPERATERAINFOREST_GLSL
#define ENDURANCE_VOLUME_TEMPERATERAINFOREST_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_TemperateRainforest = vec4(-40,12,138,.35);
const vec4 FIELD_SHAPE_TemperateRainforest = vec4(.25,.25,.50,.17);
const vec4 FIELD_STYLE_TemperateRainforest = vec4(.70,.15,.50,.25);

float fieldVolumeTemperateRainforest(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_TemperateRainforest,
                                 FIELD_SHAPE_TemperateRainforest,FIELD_STYLE_TemperateRainforest);
}

#endif
#endif
