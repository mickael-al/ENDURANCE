#version 450 core
// Biome 6: ColdSteppe. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#define FIELD_BIOME_ID 6u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME = vec4(-38,3,56,.60);
const vec4 FIELD_SHAPE = vec4(.70,.10,.20,.08);
const vec4 FIELD_STYLE = vec4(.95,.20,.15,.10);

float fieldVolume(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME,
                                 FIELD_SHAPE,FIELD_STYLE);
}

#include "../../field_volume_accumulate.glsl"
