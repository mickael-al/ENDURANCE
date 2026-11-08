// Diagnostic/reference dispatcher. Production volume passes include only their
// own biomes/<Name>/volume.glsl, never this all-biome aggregate.
#ifndef ENDURANCE_BIOME_VOLUME_GLSL
#define ENDURANCE_BIOME_VOLUME_GLSL

#define FIELD_ENABLE_VOLUME_PolarIce
#include "biomes/PolarIce/volume.glsl"
#define FIELD_ENABLE_VOLUME_FrozenTundra
#include "biomes/FrozenTundra/volume.glsl"
#define FIELD_ENABLE_VOLUME_Tundra
#include "biomes/Tundra/volume.glsl"
#define FIELD_ENABLE_VOLUME_Taiga
#include "biomes/Taiga/volume.glsl"
#define FIELD_ENABLE_VOLUME_BorealBog
#include "biomes/BorealBog/volume.glsl"
#define FIELD_ENABLE_VOLUME_RockyTundra
#include "biomes/RockyTundra/volume.glsl"
#define FIELD_ENABLE_VOLUME_ColdSteppe
#include "biomes/ColdSteppe/volume.glsl"
#define FIELD_ENABLE_VOLUME_BorealForest
#include "biomes/BorealForest/volume.glsl"
#define FIELD_ENABLE_VOLUME_ColdForest
#include "biomes/ColdForest/volume.glsl"
#define FIELD_ENABLE_VOLUME_ColdMarsh
#include "biomes/ColdMarsh/volume.glsl"
#define FIELD_ENABLE_VOLUME_DryPlain
#include "biomes/DryPlain/volume.glsl"
#define FIELD_ENABLE_VOLUME_Meadow
#include "biomes/Meadow/volume.glsl"
#define FIELD_ENABLE_VOLUME_TemperateForest
#include "biomes/TemperateForest/volume.glsl"
#define FIELD_ENABLE_VOLUME_TemperateRainforest
#include "biomes/TemperateRainforest/volume.glsl"
#define FIELD_ENABLE_VOLUME_TemperateMarsh
#include "biomes/TemperateMarsh/volume.glsl"
#define FIELD_ENABLE_VOLUME_RockDesert
#include "biomes/RockDesert/volume.glsl"
#define FIELD_ENABLE_VOLUME_DrySavanna
#include "biomes/DrySavanna/volume.glsl"
#define FIELD_ENABLE_VOLUME_Savanna
#include "biomes/Savanna/volume.glsl"
#define FIELD_ENABLE_VOLUME_DryJungle
#include "biomes/DryJungle/volume.glsl"
#define FIELD_ENABLE_VOLUME_TropicalJungle
#include "biomes/TropicalJungle/volume.glsl"
#define FIELD_ENABLE_VOLUME_VolcanicDesert
#include "biomes/VolcanicDesert/volume.glsl"
#define FIELD_ENABLE_VOLUME_RedDesert
#include "biomes/RedDesert/volume.glsl"
#define FIELD_ENABLE_VOLUME_AridPlateau
#include "biomes/AridPlateau/volume.glsl"
#define FIELD_ENABLE_VOLUME_BurningJungle
#include "biomes/BurningJungle/volume.glsl"
#define FIELD_ENABLE_VOLUME_TropicalMangrove
#include "biomes/TropicalMangrove/volume.glsl"
#define FIELD_ENABLE_VOLUME_Marine
#include "biomes/Marine/volume.glsl"

float fieldBiomeDensityDelta(uint biome, vec3 p, FieldColumn column)
{
    switch (biome)
    {
        case 0u: return fieldVolumePolarIce(p,column);
        case 1u: return fieldVolumeFrozenTundra(p,column);
        case 2u: return fieldVolumeTundra(p,column);
        case 3u: return fieldVolumeTaiga(p,column);
        case 4u: return fieldVolumeBorealBog(p,column);
        case 5u: return fieldVolumeRockyTundra(p,column);
        case 6u: return fieldVolumeColdSteppe(p,column);
        case 7u: return fieldVolumeBorealForest(p,column);
        case 8u: return fieldVolumeColdForest(p,column);
        case 9u: return fieldVolumeColdMarsh(p,column);
        case 10u: return fieldVolumeDryPlain(p,column);
        case 11u: return fieldVolumeMeadow(p,column);
        case 12u: return fieldVolumeTemperateForest(p,column);
        case 13u: return fieldVolumeTemperateRainforest(p,column);
        case 14u: return fieldVolumeTemperateMarsh(p,column);
        case 15u: return fieldVolumeRockDesert(p,column);
        case 16u: return fieldVolumeDrySavanna(p,column);
        case 17u: return fieldVolumeSavanna(p,column);
        case 18u: return fieldVolumeDryJungle(p,column);
        case 19u: return fieldVolumeTropicalJungle(p,column);
        case 20u: return fieldVolumeVolcanicDesert(p,column);
        case 21u: return fieldVolumeRedDesert(p,column);
        case 22u: return fieldVolumeAridPlateau(p,column);
        case 23u: return fieldVolumeBurningJungle(p,column);
        case 24u: return fieldVolumeTropicalMangrove(p,column);
        case 25u: return fieldVolumeMarine(p,column);
        default: return 0.0;
    }
}

// Legacy diagnostic controls. The recipe files above remain the only editable
// source of each value. This table is not part of production shader variants.
const vec4 FIELD_BIOME_VOLUME[26][3]=vec4[26][3](
    vec4[3](FIELD_VOLUME_PolarIce,FIELD_SHAPE_PolarIce,FIELD_STYLE_PolarIce),
    vec4[3](FIELD_VOLUME_FrozenTundra,FIELD_SHAPE_FrozenTundra,FIELD_STYLE_FrozenTundra),
    vec4[3](FIELD_VOLUME_Tundra,FIELD_SHAPE_Tundra,FIELD_STYLE_Tundra),
    vec4[3](FIELD_VOLUME_Taiga,FIELD_SHAPE_Taiga,FIELD_STYLE_Taiga),
    vec4[3](FIELD_VOLUME_BorealBog,FIELD_SHAPE_BorealBog,FIELD_STYLE_BorealBog),
    vec4[3](FIELD_VOLUME_RockyTundra,FIELD_SHAPE_RockyTundra,FIELD_STYLE_RockyTundra),
    vec4[3](FIELD_VOLUME_ColdSteppe,FIELD_SHAPE_ColdSteppe,FIELD_STYLE_ColdSteppe),
    vec4[3](FIELD_VOLUME_BorealForest,FIELD_SHAPE_BorealForest,FIELD_STYLE_BorealForest),
    vec4[3](FIELD_VOLUME_ColdForest,FIELD_SHAPE_ColdForest,FIELD_STYLE_ColdForest),
    vec4[3](FIELD_VOLUME_ColdMarsh,FIELD_SHAPE_ColdMarsh,FIELD_STYLE_ColdMarsh),
    vec4[3](FIELD_VOLUME_DryPlain,FIELD_SHAPE_DryPlain,FIELD_STYLE_DryPlain),
    vec4[3](FIELD_VOLUME_Meadow,FIELD_SHAPE_Meadow,FIELD_STYLE_Meadow),
    vec4[3](FIELD_VOLUME_TemperateForest,FIELD_SHAPE_TemperateForest,FIELD_STYLE_TemperateForest),
    vec4[3](FIELD_VOLUME_TemperateRainforest,FIELD_SHAPE_TemperateRainforest,FIELD_STYLE_TemperateRainforest),
    vec4[3](FIELD_VOLUME_TemperateMarsh,FIELD_SHAPE_TemperateMarsh,FIELD_STYLE_TemperateMarsh),
    vec4[3](FIELD_VOLUME_RockDesert,FIELD_SHAPE_RockDesert,FIELD_STYLE_RockDesert),
    vec4[3](FIELD_VOLUME_DrySavanna,FIELD_SHAPE_DrySavanna,FIELD_STYLE_DrySavanna),
    vec4[3](FIELD_VOLUME_Savanna,FIELD_SHAPE_Savanna,FIELD_STYLE_Savanna),
    vec4[3](FIELD_VOLUME_DryJungle,FIELD_SHAPE_DryJungle,FIELD_STYLE_DryJungle),
    vec4[3](FIELD_VOLUME_TropicalJungle,FIELD_SHAPE_TropicalJungle,FIELD_STYLE_TropicalJungle),
    vec4[3](FIELD_VOLUME_VolcanicDesert,FIELD_SHAPE_VolcanicDesert,FIELD_STYLE_VolcanicDesert),
    vec4[3](FIELD_VOLUME_RedDesert,FIELD_SHAPE_RedDesert,FIELD_STYLE_RedDesert),
    vec4[3](FIELD_VOLUME_AridPlateau,FIELD_SHAPE_AridPlateau,FIELD_STYLE_AridPlateau),
    vec4[3](FIELD_VOLUME_BurningJungle,FIELD_SHAPE_BurningJungle,FIELD_STYLE_BurningJungle),
    vec4[3](FIELD_VOLUME_TropicalMangrove,FIELD_SHAPE_TropicalMangrove,FIELD_STYLE_TropicalMangrove),
    vec4[3](FIELD_VOLUME_Marine,FIELD_SHAPE_Marine,FIELD_STYLE_Marine)

);

// Compatibility entry point for old single-recipe probes.
float fieldDensityAtPrepared(vec3 p,vec2 terrain,vec4 volume,vec4 shape,vec4 style)
{
    return fieldRecipeDensityAtPrepared(p,terrain,volume,shape,style);
}

#endif
