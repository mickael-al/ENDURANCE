#ifndef ENDURANCE_SEA_RENDERER_HPP
#define ENDURANCE_SEA_RENDERER_HPP

#include "ProceduralTerrainSea.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ptrClass;

namespace Endurance
{
    // Game-owned water appearance/rendering. The terrain supplies only published
    // sea tiles; no water shader, material or scene object belongs to the mesher.
    class SeaRenderer final
    {
    public:
        struct Stats
        {
            std::uint32_t tiles = 0;
            std::uint64_t uploads = 0, uploadBytes = 0, allocatedBytes = 0;
            std::uint64_t draws = 0;
        };
        SeaRenderer();
        ~SeaRenderer();
        SeaRenderer(const SeaRenderer&) = delete;
        SeaRenderer& operator=(const SeaRenderer&) = delete;
        // All methods require the owning GL context. The engine outlives this.
        bool initialize(const ptrClass& engine);
        bool setTiles(const std::vector<ProceduralTerrain::SeaTile>& tiles,
            std::uint64_t revision, float seaLevel, float baseChunkExtent);
        // Called in the engine's ordered BeforeMaterials transparent phase:
        // camera/misc/light/shadow buffers and environment textures are bound.
        void draw(bool* currentDepthWrite);
        void shutdown() noexcept;
        const std::string& error() const noexcept;
        const Stats& stats() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };
}
#endif
