#version 450 core
// Craters and ground fractures are already excavated by the height recipe.
// No additional volume band or legacy isolated lava-tube sculptures.
#define FIELD_BIOME_ID 20u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"

float fieldVolume(vec3 p, FieldColumn column)
{
    return 0.0;
}

#include "../../field_volume_accumulate.glsl"
