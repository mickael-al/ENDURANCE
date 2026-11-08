#version 450 core
// Fixed specialization: only this biome's relief source is a cache dependency.
#define FIELD_BIOME_ID 8u
#define FIELD_ENABLE_RELIEF_ColdForest 1
#define FIELD_RELIEF_FUNCTION fieldReliefColdForest
#include "../../field_batch_common.glsl"
#include "../../field_weights.glsl"
#include "relief.glsl"
#include "../../field_surface_accumulate.glsl"
