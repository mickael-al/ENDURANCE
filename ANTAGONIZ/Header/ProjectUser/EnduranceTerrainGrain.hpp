#pragma once

#include "ProceduralTerrainFineSurface.hpp"
#include <memory>
#include <string>

namespace Endurance
{
    // Bake compact facets and filterable slope moments from linear PNG data.
    // The source normal map encodes signed XY in RG, positive Z directly in B.
    std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> loadTerrainGrainImage(
        const std::string& noisePath, const std::string& normalPath);

    // Decode/bake once per process; no work during chunk generation.
    std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> terrainGrainImage();
}
