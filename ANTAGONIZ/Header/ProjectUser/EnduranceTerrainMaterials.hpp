#ifndef ENDURANCE_GAME_TERRAIN_MATERIALS_HPP
#define ENDURANCE_GAME_TERRAIN_MATERIALS_HPP

#include "ProceduralTerrainMaterials.hpp"
#include <cstddef>

namespace Endurance
{
    // The engine owns the fixed GPU representation; ENDURANCE authors the
    // colors and recipes. Editing these defaults never changes terrain density.
    ProceduralTerrain::TerrainAppearance makeTerrainAppearance();
    const char* terrainMaterialIntent(std::size_t biome) noexcept;
    const char* terrainMaterialLabel(std::size_t biome, bool wall, bool accent) noexcept;
}

#endif
