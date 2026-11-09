#ifndef ENDURANCE_PROCEDURAL_TERRAIN_BIOMES_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_BIOMES_HPP

#include <array>
#include <cstdint>

namespace ProceduralTerrain
{
    inline constexpr std::uint32_t BiomeCount = 26;
    inline constexpr std::uint32_t MarineBiome = 25;
    inline constexpr std::uint32_t FieldColumnBytes = 32;
    inline constexpr std::uint32_t FieldScratchColumnBytes = 48;
    // Stable IDs also select the independent 2D/3D compute source directories.
    inline constexpr std::array<const char*, BiomeCount> BiomeNames{{
        "PolarIce", "FrozenTundra", "Tundra", "Taiga", "BorealBog",
        "RockyTundra", "ColdSteppe", "BorealForest", "ColdForest", "ColdMarsh",
        "DryPlain", "Meadow", "TemperateForest", "TemperateRainforest", "TemperateMarsh",
        "RockDesert", "DrySavanna", "Savanna", "DryJungle", "TropicalJungle",
        "VolcanicDesert", "RedDesert", "AridPlateau", "BurningJungle", "TropicalMangrove", "Marine"
    }};
    static_assert(BiomeCount > 0 && BiomeCount <= 32, "Biome IDs must fit the 5-bit IDs and 32-bit mask");

    // Authoritative matrices live on the CPU. X = humidity, Y = temperature,
    // Z = macro-height relative to the shoreline; not the voxel's altitude.
    inline constexpr std::array<std::uint32_t, 25> BiomeMatrix2D{{
        0,1,2,3,4, 5,6,7,8,9, 10,11,12,13,14, 15,16,17,18,19, 20,21,22,23,24
    }};
    inline constexpr std::array<std::uint32_t, 125> BiomeMatrix3D{{
        25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24
    }};

    // std430 uvec4: surface IDs, two words of 5-bit volume IDs, then a mask
    // of the distinct volume biomes. The mask includes zero-weight candidates;
    // local climate/altitude ratios are still evaluated on the GPU as floats.
    using BiomeCell = std::array<std::uint32_t, 4>;
    using BiomeCellTable = std::array<BiomeCell, 64>;
    static_assert(sizeof(BiomeCell) == 16 && sizeof(BiomeCellTable) == 1024,
                  "Biome masks must reuse the existing GPU table layout");

    constexpr bool validBiomeMatrices() noexcept
    {
        for (auto id : BiomeMatrix2D) if (id >= BiomeCount) return false;
        for (auto id : BiomeMatrix3D) if (id >= BiomeCount) return false;
        return true;
    }
    static_assert(validBiomeMatrices(), "Biome matrix entries must name a defined biome");

    constexpr BiomeCellTable makeBiomeCellTable() noexcept
    {
        BiomeCellTable result{};
        for (std::uint32_t z=0; z<4; ++z)
        for (std::uint32_t y=0; y<4; ++y)
        for (std::uint32_t x=0; x<4; ++x)
        {
            auto& cell=result[x+4*y+16*z];
            for (std::uint32_t dy=0; dy<2; ++dy)
            for (std::uint32_t dx=0; dx<2; ++dx)
                cell[0] |= BiomeMatrix2D[x+dx+5*(y+dy)] << (5*(dx+2*dy));
            for (std::uint32_t dz=0; dz<2; ++dz)
            for (std::uint32_t dy=0; dy<2; ++dy)
            for (std::uint32_t dx=0; dx<2; ++dx)
            {
                const auto corner=dx+2*dy+4*dz;
                const auto id=BiomeMatrix3D[x+dx+5*(y+dy)+25*(z+dz)];
                cell[corner<6 ? 1 : 2] |= id << (5*(corner<6 ? corner : corner-6));
                cell[3] |= std::uint32_t{1} << id;
            }
        }
        return result;
    }
}

#endif
