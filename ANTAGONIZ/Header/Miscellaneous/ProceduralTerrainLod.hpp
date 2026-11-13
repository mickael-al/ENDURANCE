#ifndef ENDURANCE_PROCEDURAL_TERRAIN_LOD_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_LOD_HPP

#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ProceduralTerrain
{
    struct LodChunk
    {
        std::array<std::int32_t, 3> origin{};
        std::uint32_t lod = 0;
        // Fine side owns each transition. -X,+X,-Y,+Y,-Z,+Z.
        std::uint32_t transitionMask = 0;
    };

    struct LodConfig
    {
        std::uint32_t cells = 64;
        std::uint32_t maxLod = 6;
        float baseSpacing = 1.0f;
        float viewDistance = 4096.0f;
        float splitDistanceFactor = 1.25f;
        float minimumY = -1024.0f;
        float maximumY = 1024.0f;
        float fullResolutionRadius = 128.0f;
        // The +/-4096 raw height envelope needs 1097 leaves at the default
        // fixture camera. This bounds selection metadata, not GPU vertex memory.
        std::uint32_t maxLeaves = 2048;
    };

    // Select a globally aligned, face-balanced octree. A failed selection leaves
    // output unchanged; callers can retain their previous complete terrain.
    bool selectLodChunks(const glm::vec3& camera, const LodConfig& config,
                         std::vector<LodChunk>& output, std::string& error);
    double chunkDistanceSquared(const glm::vec3& position, const LodChunk& chunk,
                                std::uint32_t cells, float baseSpacing) noexcept;
}

#endif
