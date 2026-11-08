#version 450 core
// Biome 23: BurningJungle. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#define FIELD_BIOME_ID 23u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME = vec4(-64,18,180,.30);
const vec4 FIELD_SHAPE = vec4(.25,.30,.45,.19);
const vec4 FIELD_STYLE = vec4(.75,.45,.70,.65);

float fieldVolume(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME,
                                 FIELD_SHAPE,FIELD_STYLE);
}

#include "../../field_volume_accumulate.glsl"
