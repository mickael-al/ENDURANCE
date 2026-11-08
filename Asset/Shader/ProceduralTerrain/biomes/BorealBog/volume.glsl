// Biome 4: BorealBog. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_BorealBog
#ifndef ENDURANCE_VOLUME_BOREALBOG_GLSL
#define ENDURANCE_VOLUME_BOREALBOG_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_BorealBog = vec4(-18,2,40,.65);
const vec4 FIELD_SHAPE_BorealBog = vec4(.15,.70,.15,.09);
const vec4 FIELD_STYLE_BorealBog = vec4(.95,.00,.15,.30);

float fieldVolumeBorealBog(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_BorealBog,
                                 FIELD_SHAPE_BorealBog,FIELD_STYLE_BorealBog);
}

#endif
#endif
