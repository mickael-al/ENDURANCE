#pragma once

#include "ProceduralTerrainMaterials.hpp"
#ifndef JS_STL_ARRAY
#define JS_STL_ARRAY
#endif
#include "json_struct.h"

// Serialization belongs to the game. The renderer's fixed GPU records remain
// independent of the editor, JSON library and ENDURANCE's authored palettes.
JS_OBJ_EXT(ProceduralTerrain::TerrainMaterial,
    colorLow, colorMid, colorHigh, response, ramp, pattern);
JS_OBJ_EXT(ProceduralTerrain::TerrainSurface, base, accent, blend, detail, normal);
JS_OBJ_EXT(ProceduralTerrain::TerrainBiomeAppearance, ground, wall, slope, spectrum);
JS_OBJ_EXT(ProceduralTerrain::TerrainAppearance, biomes, macroPeriod, patchPeriod, debugView);
