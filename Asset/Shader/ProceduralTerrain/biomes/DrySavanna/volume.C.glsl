#version 450 core
// Fixed specialization: only this biome's volume source is a cache dependency.
#define FIELD_BIOME_ID 16u
#define FIELD_ENABLE_VOLUME_DrySavanna 1
#define FIELD_VOLUME_FUNCTION fieldVolumeDrySavanna
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "volume.glsl"
#include "../../field_volume_accumulate.glsl"
