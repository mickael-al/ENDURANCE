#include "ProceduralTerrainSea.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace ProceduralTerrain;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

LodChunk chunk(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t lod)
{
    return {{{x, y, z}}, lod, 0};
}

bool rejected(const LodChunk& leaf, std::uint32_t cells, float spacing, float level, bool marine = true)
{
    SeaTile output{71, -93, 19, 777}, before = output;
    const bool accepted = seaTileForChunk(leaf, cells, spacing, level, marine, output);
    if (!accepted) require(std::memcmp(&output, &before, sizeof(output)) == 0, "rejection changed output");
    return !accepted;
}

std::vector<SeaTile> cut(const std::vector<LodChunk>& leaves, float level)
{
    std::vector<SeaTile> tiles;
    for (const auto& leaf : leaves)
    {
        SeaTile tile;
        if (seaTileForChunk(leaf, 8, 1.0f, level, true, tile)) tiles.push_back(tile);
    }
    return tiles;
}

std::vector<LodChunk> split(const LodChunk& parent)
{
    require(parent.lod > 0, "cannot split finest leaf");
    const auto extent = static_cast<std::int32_t>(8u << (parent.lod - 1));
    std::vector<LodChunk> children;
    for (int y = 0; y != 2; ++y)
    for (int z = 0; z != 2; ++z)
    for (int x = 0; x != 2; ++x)
        children.push_back(chunk(parent.origin[0] + x * extent,
            parent.origin[1] + y * extent, parent.origin[2] + z * extent, parent.lod - 1));
    return children;
}

void sameCoverage(const std::vector<SeaTile>& actual, const SeaTile& expected)
{
    const auto span = std::int64_t{1} << expected.lod;
    require(span <= 32, "fixture footprint too large");
    std::vector<unsigned> counts(static_cast<std::size_t>(span * span), 0);
    for (const auto& tile : actual)
    {
        const auto width = std::int64_t{1} << tile.lod;
        const auto ox = static_cast<std::int64_t>(tile.baseX) - expected.baseX;
        const auto oz = static_cast<std::int64_t>(tile.baseZ) - expected.baseZ;
        require(ox >= 0 && oz >= 0 && ox + width <= span && oz + width <= span,
            "replacement escaped original footprint");
        for (std::int64_t z = oz; z != oz + width; ++z)
        for (std::int64_t x = ox; x != ox + width; ++x)
            ++counts[static_cast<std::size_t>(x + span * z)];
    }
    for (auto count : counts) require(count == 1, "split/merge leaves a water gap or overlap");
}

// CPU reference for the water VS contract: the same numerator and scale as
// render.vs.glsl for a packed terrain boundary vertex (code 0 or 65534).
float decode(std::int32_t base, std::uint32_t lod, float corner, float baseExtent)
{
    const float scale = static_cast<float>(std::uint32_t{1} << lod);
    const float numerator = std::fma(static_cast<float>(base), 65534.0f, (corner * 65534.0f) * scale);
    return numerator * (baseExtent / 65534.0f);
}

void sameBits(float a, float b, const char* message)
{
    std::uint32_t x, y;
    std::memcpy(&x, &a, sizeof(x)); std::memcpy(&y, &b, sizeof(y));
    require(x == y, message);
}
}

int main()
{
    SeaTile tile;
    require(seaTileForChunk(chunk(0, 0, 0, 0), 8, 1, 0, true, tile), "lower boundary not owned");
    require(tile.baseX == 0 && tile.baseZ == 0 && tile.lod == 0 && tile.padding == 0, "instance encoding");
    require(rejected(chunk(0, -8, 0, 0), 8, 1, 0), "upper boundary owned twice");
    require(seaTileForChunk(chunk(-16, -16, -32, 1), 8, .5f, -4, true, tile), "negative lattice or scaled height");
    require(tile.baseX == -2 && tile.baseZ == -4 && tile.lod == 1, "negative integer encoding");
    require(rejected(chunk(-16, -16, -32, 1), 8, .5f, 0), "negative upper edge duplicated");
    require(rejected(chunk(0, 0, 0, 0), 8, 1, 4, false), "nonmarine tile accepted");
    require(rejected(chunk(0, 0, 0, 0), 8, 1, 9), "sea outside chunk accepted");
    // Mesh vertex count is deliberately not an argument. An empty air chunk
    // above a deep seabed must still receive its full square when marine.
    require(seaTileForChunk(chunk(16, 0, -16, 0), 8, 1, 4, true, tile), "empty marine chunk cannot make water");

    for (std::uint32_t cells : {4u, 8u, 16u, 32u, 64u})
    {
        require(seaTileForChunk(chunk(0, 0, 0, 0), cells, .1f, 0, true, tile), "valid cell count rejected");
        for (float level : {-64.0f, -32.0f, -0.25f, 0.0f, 0.25f, 32.0f, 64.0f})
        {
            unsigned owners = 0;
            for (int y = -128; y < 128; y += static_cast<int>(cells))
                owners += seaTileForChunk(chunk(0, y, 0, 0), cells, 1, level, true, tile);
            require(owners == 1, "half-open stacked cubes do not have one owner");
        }
    }

    for (const auto& parent : {chunk(0, 0, 0, 3), chunk(-64, -64, -128, 3)})
    {
        const auto children = split(parent);
        for (float relative : {0.0f, 15.75f, 31.99f, 32.0f, 63.99f})
        {
            const float level = static_cast<float>(parent.origin[1]) + relative;
            const auto coarse = cut({parent}, level);
            const auto fine = cut(children, level);
            require(coarse.size() == 1 && fine.size() == 4, "split must replace one square by four");
            sameCoverage(coarse, coarse[0]);
            sameCoverage(fine, coarse[0]);
            // Refine just one of the intersecting children, leaving a genuine
            // 2:1 boundary in the water plane, then merge back to the parent.
            auto mixed = children;
            const unsigned selected = relative < 32 ? 0 : 4;
            auto grandchildren = split(mixed[selected]);
            mixed.erase(mixed.begin() + selected);
            mixed.insert(mixed.end(), grandchildren.begin(), grandchildren.end());
            sameCoverage(cut(mixed, level), coarse[0]);
            sameCoverage(cut({parent}, level), coarse[0]);
        }
    }

    for (float spacing : {.1f, 1.0f, 3.7f, 17.125f})
    for (std::uint32_t lod = 1; lod <= 20; ++lod)
    {
        const auto scale = static_cast<std::int32_t>(std::uint32_t{1} << lod);
        for (std::int32_t base : {-8388608, -2097152, 0, 2097152, 8388608})
        {
            const auto aligned = (base / scale) * scale;
            const float extent = 64.0f * spacing;
            sameBits(decode(aligned, lod, 1, extent), decode(aligned + scale, lod - 1, 0, extent),
                "coarse/fine boundary differs in float bits");
            sameBits(decode(aligned, lod, .5f, extent), decode(aligned + scale / 2, lod - 1, 0, extent),
                "coarse edge midpoint differs from fine corner");
            const float lodScale = static_cast<float>(scale);
            const float meshExtent = 64.0f * (lodScale * spacing);
            sameBits(extent, meshExtent / lodScale, "sea base extent differs from terrain base extent");
        }
    }

    const auto valid = chunk(0, 0, 0, 0);
    require(rejected(valid, 0, 1, 0) && rejected(valid, 3, 1, 0) && rejected(valid, 12, 1, 0) &&
        rejected(valid, 128, 1, 0), "invalid cell counts accepted");
    require(rejected(chunk(0, 0, 0, 21), 8, 1, 0), "oversized LOD accepted");
    require(rejected(chunk(1, 0, 0, 0), 8, 1, 0) && rejected(chunk(8, 0, 0, 1), 8, 1, 0),
        "unaligned chunk accepted");
    require(rejected(chunk(INT32_MIN, 0, 0, 0), 64, 1, 0), "negative halo overflow accepted");
    require(rejected(chunk(INT32_MAX - 63, 0, 0, 0), 64, 1, 0), "positive halo overflow accepted");
    require(rejected(chunk((16777216 + 2) * 8, 0, 0, 0), 8, 1, 0), "inexact base coordinate accepted");
    require(rejected(valid, 8, 0, 0) && rejected(valid, 8, -1, 0), "invalid spacing accepted");
    require(rejected(valid, 8, (std::numeric_limits<float>::min)(), 0), "subnormal decode scale accepted");
    require(rejected(valid, 8, (std::numeric_limits<float>::max)(), 0), "overflowing world domain accepted");
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(rejected(valid, 8, infinity, 0) && rejected(valid, 8, nan, 0) &&
        rejected(valid, 8, 1, infinity) && rejected(valid, 8, 1, nan), "nonfinite input accepted");
    std::cout << "PASS sea coverage: marine squares, empty leaves, half-open heights, split/merge, mixed LOD, canonical seams and invalid inputs\n";
}
