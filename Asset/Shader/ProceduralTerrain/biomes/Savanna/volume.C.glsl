#version 450 core
// Biome 17: Savanna. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#define FIELD_BIOME_ID 17u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME = vec4(-40,8,105,.45);
const vec4 FIELD_SHAPE = vec4(.55,.15,.30,.12);
const vec4 FIELD_STYLE = vec4(.80,.20,.30,.15);

float fieldVolume(vec3 p, FieldColumn column)
{
    // This biome owns the meaning of the extra 2D control (relief.w).
    // Replace these amplitude factors with local rock placement, cavities or
    // another construction here; the accumulation kernel does not interpret it.
    vec4 volume=FIELD_VOLUME;
    volume.yz*=uDetailScale;
    volume.y*=mix(0.55,1.0,column.blend.w);
    volume.z*=mix(0.85,1.0,column.blend.w);
    return fieldVolumeRecipeDelta(p,column,volume,FIELD_SHAPE,FIELD_STYLE);
}

#include "../../field_volume_accumulate.glsl"
