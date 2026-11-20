#ifndef ENDURANCE_PROCEDURAL_TERRAIN_SEA_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_SEA_HPP

#include "ProceduralTerrainField.hpp"
#include "ProceduralTerrainLod.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ProceduralTerrain
{
    // One instance of the shared sea quad. Integer finest-chunk coordinates
    // preserve the terrain renderer's canonical FMA decode at LOD boundaries;
    // storing rounded world origins plus an extent would not preserve seams.
    // GPU: ivec2 baseXZ; uint lod; uint padding (std430 stride 16).
    struct alignas(16) SeaTile
    {
        std::int32_t baseX = 0;
        std::int32_t baseZ = 0;
        std::uint32_t lod = 0;
        std::uint32_t padding = 0;
    };
    static_assert(sizeof(SeaTile) == 16 && alignof(SeaTile) == 16 &&
        offsetof(SeaTile, baseX) == 0 && offsetof(SeaTile, baseZ) == 4 &&
        offsetof(SeaTile, lod) == 8 && offsetof(SeaTile, padding) == 12 &&
        std::is_trivially_copyable<SeaTile>::value, "Sea instance GPU layout changed");

    // Consume only the atomically published octree leaves, including leaves
    // with no terrain vertices. Marine presence selects the entire XZ square:
    // there is no coastal shape, fractional mask or per-pixel biome decision.
    // A half-open vertical interval gives a sea-level boundary to exactly one
    // layer of cubes. Failure leaves output untouched.
    inline bool seaTileForChunk(const LodChunk& chunk, std::uint32_t cells,
        float baseSpacing, float seaLevel, bool hasMarine, SeaTile& output) noexcept
    {
        if (!hasMarine || cells < 4 || cells > 64 || (cells & (cells - 1)) != 0 ||
            chunk.lod > 20 || !finiteFieldValue(baseSpacing) ||
            !finiteFieldValue(seaLevel) || baseSpacing <= 0.0f ||
            baseSpacing < (std::numeric_limits<float>::min)() * 65534.0f / cells)
            return false;

        const std::int64_t step = std::int64_t{1} << chunk.lod;
        const std::int64_t span = static_cast<std::int64_t>(cells) * step;
        // Match GpuMesher::submit's aligned lattice, halo and exactly
        // representable finest-chunk coordinate domain before integer casts.
        for (std::int32_t origin : chunk.origin)
        {
            const auto value = static_cast<std::int64_t>(origin);
            const auto base = value / cells;
            if (value % span != 0 || value - step < INT32_MIN ||
                value + (static_cast<std::int64_t>(cells) + 1) * step > INT32_MAX ||
                base < -(std::int64_t{1} << 24) || base > (std::int64_t{1} << 24))
                return false;
            const double lower = static_cast<double>(value - step) * baseSpacing;
            const double upper = static_cast<double>(value + span + step) * baseSpacing;
            if (lower < -0x1p60 || lower > 0x1p60 || upper < -0x1p60 || upper > 0x1p60)
                return false;
        }

        // Determine ownership from the mathematical lattice in double, not
        // separately rounded float world bounds on either side of a split.
        const double minimumY = static_cast<double>(chunk.origin[1]) * baseSpacing;
        const double maximumY = static_cast<double>(
            static_cast<std::int64_t>(chunk.origin[1]) + span) * baseSpacing;
        if (static_cast<double>(seaLevel) < minimumY ||
            static_cast<double>(seaLevel) >= maximumY)
            return false;

        output = {static_cast<std::int32_t>(static_cast<std::int64_t>(chunk.origin[0]) / cells),
            static_cast<std::int32_t>(static_cast<std::int64_t>(chunk.origin[2]) / cells),
            chunk.lod, 0};
        return true;
    }
}

#endif
