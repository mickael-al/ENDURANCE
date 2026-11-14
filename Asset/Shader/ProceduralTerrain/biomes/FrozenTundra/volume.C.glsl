#version 450 core
#define FIELD_BIOME_ID 1u
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"

// Fresh mountain-chain biome. Previous pingos/caves remain removed.
float fieldVolume(vec3 p, FieldColumn column)
{
    return 0.0;
}

#include "../../field_volume_accumulate.glsl"
