// Diagnostic/reference dispatcher. Production passes include only one
// biomes/<Name>/relief.glsl file, so unrelated recipes never enter their cache key.
#ifndef ENDURANCE_BIOME_RELIEF_GLSL
#define ENDURANCE_BIOME_RELIEF_GLSL

#define FIELD_ENABLE_RELIEF_PolarIce
#include "biomes/PolarIce/relief.glsl"
#define FIELD_ENABLE_RELIEF_FrozenTundra
#include "biomes/FrozenTundra/relief.glsl"
#define FIELD_ENABLE_RELIEF_Tundra
#include "biomes/Tundra/relief.glsl"
#define FIELD_ENABLE_RELIEF_Taiga
#include "biomes/Taiga/relief.glsl"
#define FIELD_ENABLE_RELIEF_BorealBog
#include "biomes/BorealBog/relief.glsl"
#define FIELD_ENABLE_RELIEF_RockyTundra
#include "biomes/RockyTundra/relief.glsl"
#define FIELD_ENABLE_RELIEF_ColdSteppe
#include "biomes/ColdSteppe/relief.glsl"
#define FIELD_ENABLE_RELIEF_BorealForest
#include "biomes/BorealForest/relief.glsl"
#define FIELD_ENABLE_RELIEF_ColdForest
#include "biomes/ColdForest/relief.glsl"
#define FIELD_ENABLE_RELIEF_ColdMarsh
#include "biomes/ColdMarsh/relief.glsl"
#define FIELD_ENABLE_RELIEF_DryPlain
#include "biomes/DryPlain/relief.glsl"
#define FIELD_ENABLE_RELIEF_Meadow
#include "biomes/Meadow/relief.glsl"
#define FIELD_ENABLE_RELIEF_TemperateForest
#include "biomes/TemperateForest/relief.glsl"
#define FIELD_ENABLE_RELIEF_TemperateRainforest
#include "biomes/TemperateRainforest/relief.glsl"
#define FIELD_ENABLE_RELIEF_TemperateMarsh
#include "biomes/TemperateMarsh/relief.glsl"
#define FIELD_ENABLE_RELIEF_RockDesert
#include "biomes/RockDesert/relief.glsl"
#define FIELD_ENABLE_RELIEF_DrySavanna
#include "biomes/DrySavanna/relief.glsl"
#define FIELD_ENABLE_RELIEF_Savanna
#include "biomes/Savanna/relief.glsl"
#define FIELD_ENABLE_RELIEF_DryJungle
#include "biomes/DryJungle/relief.glsl"
#define FIELD_ENABLE_RELIEF_TropicalJungle
#include "biomes/TropicalJungle/relief.glsl"
#define FIELD_ENABLE_RELIEF_VolcanicDesert
#include "biomes/VolcanicDesert/relief.glsl"
#define FIELD_ENABLE_RELIEF_RedDesert
#include "biomes/RedDesert/relief.glsl"
#define FIELD_ENABLE_RELIEF_AridPlateau
#include "biomes/AridPlateau/relief.glsl"
#define FIELD_ENABLE_RELIEF_BurningJungle
#include "biomes/BurningJungle/relief.glsl"
#define FIELD_ENABLE_RELIEF_TropicalMangrove
#include "biomes/TropicalMangrove/relief.glsl"
#define FIELD_ENABLE_RELIEF_Marine
#include "biomes/Marine/relief.glsl"

// Compatibility names for older diagnostic probes; recipes use canonical IDs.
vec4 fieldReliefBorealWetland(vec2 p) { return fieldReliefBorealBog(p); }
vec4 fieldReliefPrairie(vec2 p) { return fieldReliefMeadow(p); }
vec4 fieldReliefHumidTemperateForest(vec2 p) { return fieldReliefTemperateRainforest(p); }

vec4 fieldBiomeRelief(uint biome, vec2 p)
{
    vec4 result;
    switch (biome)
    {
        case 0u: result = fieldReliefPolarIce(p); break;
        case 1u: result = fieldReliefFrozenTundra(p); break;
        case 2u: result = fieldReliefTundra(p); break;
        case 3u: result = fieldReliefTaiga(p); break;
        case 4u: result = fieldReliefBorealBog(p); break;
        case 5u: result = fieldReliefRockyTundra(p); break;
        case 6u: result = fieldReliefColdSteppe(p); break;
        case 7u: result = fieldReliefBorealForest(p); break;
        case 8u: result = fieldReliefColdForest(p); break;
        case 9u: result = fieldReliefColdMarsh(p); break;
        case 10u: result = fieldReliefDryPlain(p); break;
        case 11u: result = fieldReliefMeadow(p); break;
        case 12u: result = fieldReliefTemperateForest(p); break;
        case 13u: result = fieldReliefTemperateRainforest(p); break;
        case 14u: result = fieldReliefTemperateMarsh(p); break;
        case 15u: result = fieldReliefRockDesert(p); break;
        case 16u: result = fieldReliefDrySavanna(p); break;
        case 17u: result = fieldReliefSavanna(p); break;
        case 18u: result = fieldReliefDryJungle(p); break;
        case 19u: result = fieldReliefTropicalJungle(p); break;
        case 20u: result = fieldReliefVolcanicDesert(p); break;
        case 21u: result = fieldReliefRedDesert(p); break;
        case 22u: result = fieldReliefAridPlateau(p); break;
        case 23u: result = fieldReliefBurningJungle(p); break;
        case 24u: result = fieldReliefTropicalMangrove(p); break;
        default: result = fieldReliefMarine(p); break;
    }
    return fieldClampRelief(result);
}

#endif
