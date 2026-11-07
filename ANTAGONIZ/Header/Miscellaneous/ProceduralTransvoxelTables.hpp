#ifndef ENDURANCE_PROCEDURAL_TRANSVOXEL_TABLES_HPP
#define ENDURANCE_PROCEDURAL_TRANSVOXEL_TABLES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ProceduralTerrain::Transvoxel
{
    namespace Source
    {
        // Unmodified Eric Lengyel tables, under their accompanying MIT license.
        // Keeping the original data makes the GPU transformation auditable.
        #include "../../../ThirdParty/transvoxel/Transvoxel.cpp"
    }

    inline constexpr std::uint32_t RegularOffset = 0;
    inline constexpr std::uint32_t RegularStride = 5;
    inline constexpr std::uint32_t TransitionOffset = 256 * RegularStride;
    inline constexpr std::uint32_t TransitionStride = 10;
    inline constexpr std::uint32_t WordCount = TransitionOffset + 512 * TransitionStride;
    using PackedTables = std::array<std::uint32_t, WordCount>;

    // Every case starts with its triangle-list vertex count. Remaining words
    // contain four edge endpoint pairs, one byte each, least significant first.
    // The high nibble is endpoint A, the low nibble endpoint B. Vertex reuse
    // metadata is deliberately removed: output is a compact unindexed VBO.
    // Regular corner i = (i&1, (i>>1)&1, (i>>2)&1).
    // Transition corners 0..8 form a row-major 3x3 face; 9,A,B,C duplicate
    // 0,2,6,8 on the coarse side. Transition cases use bits 0,1,2,7,8,3,6,5,4.
    inline PackedTables makePackedTables()
    {
        PackedTables result{};
        for (std::uint32_t c = 0; c != 256; ++c)
        {
            const auto& cell = Source::regularCellData[Source::regularCellClass[c]];
            const std::uint32_t count = 3u * std::uint32_t(cell.GetTriangleCount());
            const std::size_t base = RegularOffset + c * RegularStride;
            result[base] = count;
            for (std::uint32_t v = 0; v != count; ++v)
            {
                const std::uint32_t edge = Source::regularVertexData[c][cell.vertexIndex[v]] & 255u;
                result[base + 1 + v / 4] |= edge << ((v % 4) * 8);
            }
        }
        for (std::uint32_t c = 0; c != 512; ++c)
        {
            const std::uint32_t cls = Source::transitionCellClass[c];
            const auto& cell = Source::transitionCellData[cls & 127u];
            const std::uint32_t count = 3u * std::uint32_t(cell.GetTriangleCount());
            const std::size_t base = TransitionOffset + c * TransitionStride;
            result[base] = count;
            for (std::uint32_t v = 0; v != count; ++v)
            {
                // Orientation is canonical face (u,v,inward). Its transform to
                // each cube face has positive determinant. Positive density is
                // solid, and triangle winding points towards negative density.
                const std::uint32_t index = (cls & 128u) ? v : (v / 3 * 3 + 2 - v % 3);
                const std::uint32_t edge = Source::transitionVertexData[c][cell.vertexIndex[index]] & 255u;
                result[base + 1 + v / 4] |= edge << ((v % 4) * 8);
            }
        }
        return result;
    }
}

#endif
