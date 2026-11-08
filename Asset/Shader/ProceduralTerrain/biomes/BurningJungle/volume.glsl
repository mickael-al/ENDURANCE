// Biome 23: BurningJungle. This is its independently editable 3D function.
// Return a signed DELTA in metres. The pipeline adds the common base once, then
// adds localWeight * delta. Do not add the base density or another biome here.
#ifdef FIELD_ENABLE_VOLUME_BurningJungle
#ifndef ENDURANCE_VOLUME_BURNINGJUNGLE_GLSL
#define ENDURANCE_VOLUME_BURNINGJUNGLE_GLSL

#include "../../biome_volume_common.glsl"

const vec4 FIELD_VOLUME_BurningJungle = vec4(-64,18,180,.30);
const vec4 FIELD_SHAPE_BurningJungle = vec4(.25,.30,.45,.19);
const vec4 FIELD_STYLE_BurningJungle = vec4(.75,.45,.70,.65);

float fieldVolumeBurningJungle(vec3 p, FieldColumn column)
{
    return fieldVolumeRecipeDelta(p,column,FIELD_VOLUME_BurningJungle,
                                 FIELD_SHAPE_BurningJungle,FIELD_STYLE_BurningJungle);
}

#endif
#endif
