#pragma once

#include "ProceduralTerrainMaterials.hpp"

namespace Endurance
{
    // Returns true only after an edit. No renderer/resource ownership here.
    bool drawTerrainMaterialEditor(ProceduralTerrain::TerrainAppearance& appearance,
        float& microDetailStrength, float& microDetailScale, int& selectedBiome);
}
