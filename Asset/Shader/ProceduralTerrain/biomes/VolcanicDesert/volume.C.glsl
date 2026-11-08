#version 450 core
// Fixed specialization: only this biome's volume source is a cache dependency.
#define FIELD_BIOME_ID 20u
#define FIELD_ENABLE_VOLUME_VolcanicDesert 1
#define FIELD_VOLUME_FUNCTION fieldVolumeVolcanicDesert
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "volume.glsl"
#include "../../field_volume_accumulate.glsl"
