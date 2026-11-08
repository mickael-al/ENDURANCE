#version 450 core
// Fixed specialization: only this biome's volume source is a cache dependency.
#define FIELD_BIOME_ID 14u
#define FIELD_ENABLE_VOLUME_TemperateMarsh 1
#define FIELD_VOLUME_FUNCTION fieldVolumeTemperateMarsh
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "volume.glsl"
#include "../../field_volume_accumulate.glsl"
