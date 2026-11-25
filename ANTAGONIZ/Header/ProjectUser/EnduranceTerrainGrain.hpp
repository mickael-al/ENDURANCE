#pragma once

#include "ProceduralTerrainFineSurface.hpp"
#include <memory>
#include <string>

namespace Endurance
{
    // Original PNGs are data textures, not sRGB colors. The supplied normal
    // map encodes signed XY in RG and positive Z directly in B.
    std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> loadTerrainGrainImage(
        const std::string& noisePath, const std::string& normalPath);

    // Decode and pack once per process; no work during chunk generation.
    std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> terrainGrainImage();
}
