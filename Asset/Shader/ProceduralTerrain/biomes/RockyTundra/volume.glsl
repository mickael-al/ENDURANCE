// Biome 5: RockyTundra. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_RockyTundra
#ifndef ENDURANCE_VOLUME_ROCKYTUNDRA_GLSL
#define ENDURANCE_VOLUME_ROCKYTUNDRA_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_RockyTundra = vec4(-36,10,110,.35);
const vec4 FIELD_SHAPE_RockyTundra = vec4(.50,.15,.35,.12);
const vec4 FIELD_STYLE_RockyTundra = vec4(.75,.85,.50,.60);

float fieldVolumeRockyTundra(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_RockyTundra,
                                 FIELD_SHAPE_RockyTundra,FIELD_STYLE_RockyTundra);
}

#endif
#endif
