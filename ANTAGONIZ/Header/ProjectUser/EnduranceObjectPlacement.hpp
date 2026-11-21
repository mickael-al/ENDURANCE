#ifndef ENDURANCE_OBJECT_PLACEMENT_HPP
#define ENDURANCE_OBJECT_PLACEMENT_HPP
#include "ProceduralTerrainObjects.hpp"
namespace Endurance
{
    // Reads catalogue/biome settings only. Never loads mesh, PNG or tree.bin.
    bool loadObjectPlacement(const std::string& path, ProceduralTerrain::ObjectPlacementConfig& output,
        std::string& error);
}
#endif
