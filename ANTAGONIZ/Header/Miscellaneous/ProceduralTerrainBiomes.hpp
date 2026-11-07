#ifndef ENDURANCE_PROCEDURAL_TERRAIN_BIOMES_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_BIOMES_HPP

#include <array>
#include <cstdint>
#include <string>

namespace ProceduralTerrain
{
    inline constexpr std::uint32_t BiomeCount = 26;
    inline constexpr std::uint32_t MarineBiome = 25;
    inline constexpr std::uint32_t AllBiomes = (1u << BiomeCount) - 1u;
    inline constexpr std::uint32_t FieldColumnBytes = 48;

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

    inline constexpr std::array<const char*, BiomeCount> BiomeDefineNames{{
        "POLAR_ICE", "FROZEN_TUNDRA", "TUNDRA", "TAIGA", "BOREAL_BOG",
        "ROCKY_TUNDRA", "COLD_STEPPE", "BOREAL_FOREST", "COLD_FOREST", "COLD_MARSH",
        "DRY_PLAIN", "MEADOW", "TEMPERATE_FOREST", "TEMPERATE_RAINFOREST", "TEMPERATE_MARSH",
        "ROCK_DESERT", "DRY_SAVANNA", "SAVANNA", "DRY_JUNGLE", "TROPICAL_JUNGLE",
        "VOLCANIC_DESERT", "RED_DESERT", "ARID_PLATEAU", "BURNING_JUNGLE", "TROPICAL_MANGROVE", "MARINE"
    }};

    struct BiomeMasks
    {
        std::uint32_t surface = 0, volume = 0;
        constexpr std::uint64_t key() const noexcept
        { return static_cast<std::uint64_t>(surface) | (static_cast<std::uint64_t>(volume) << 32u); }
    };

    // std430 uvec4. Eight 5-bit volume IDs occupy two words, without quantizing
    // any climate coordinate, density, or local interpolation ratio.
    using BiomeCell = std::array<std::uint32_t, 4>;
    using BiomeCellTable = std::array<BiomeCell, 64>;

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
            }
        }
        return result;
    }

    constexpr std::array<BiomeMasks,64> makeBiomeCellMasks() noexcept
    {
        const auto cells=makeBiomeCellTable();
        std::array<BiomeMasks,64> result{};
        for (std::uint32_t cell=0; cell<64; ++cell)
        {
            for (unsigned corner=0; corner<4; ++corner)
                result[cell].surface |= 1u << ((cells[cell][0] >> (corner*5)) & 31u);
            if (cell<16) result[cell].surface |= 1u << MarineBiome;
            for (unsigned corner=0; corner<8; ++corner)
                result[cell].volume |= 1u << ((cells[cell][corner<6 ? 1 : 2] >> (5*(corner<6 ? corner : corner-6))) & 31u);
        }
        return result;
    }

    inline BiomeMasks biomeMasksForCells(std::uint32_t low, std::uint32_t high) noexcept
    {
        static constexpr auto masks=makeBiomeCellMasks();
        BiomeMasks result;
        // Only 64 cells; no climate data or per-column weights cross the bus.
        for (unsigned cell=0; cell<64; ++cell)
            if (((cell<32 ? low : high) >> (cell&31u)) & 1u)
            { result.surface |= masks[cell].surface; result.volume |= masks[cell].volume; }
        return result;
    }

    inline std::string biomeDefines(BiomeMasks masks)
    {
        std::string result="#define FIELD_SPECIALIZED_BIOMES 1\n";
        for (unsigned id=0; id<BiomeCount; ++id)
        {
            if (masks.surface & (1u<<id))
                result += std::string("#define BIOME_SURFACE_")+BiomeDefineNames[id]+" 1\n";
            if (masks.volume & (1u<<id))
                result += std::string("#define BIOME_VOLUME_")+BiomeDefineNames[id]+" 1\n";
        }
        return result;
    }
}

#endif
