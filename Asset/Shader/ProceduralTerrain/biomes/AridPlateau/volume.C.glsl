#version 450 core
// Biome 22: AridPlateau. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#define FIELD_BIOME_ID 22u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME = vec4(-56,10,168,.30);
const vec4 FIELD_SHAPE = vec4(.15,.70,.15,.20);
const vec4 FIELD_STYLE = vec4(.35,.70,.25,1.00);

float fieldVolume(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME,
                                 FIELD_SHAPE,FIELD_STYLE);
}

#include "../../field_volume_accumulate.glsl"
