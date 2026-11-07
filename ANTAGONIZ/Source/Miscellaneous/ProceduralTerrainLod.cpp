#include "ProceduralTerrainLod.hpp"
#include "ProceduralTerrainField.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace ProceduralTerrain
{
namespace
{
    struct Key
    {
        std::array<std::int32_t, 3> origin;
        std::uint32_t lod;
        bool operator==(const Key& b) const noexcept { return origin == b.origin && lod == b.lod; }
    };
    struct Hash
    {
        std::size_t operator()(const Key& k) const noexcept
        {
            std::uint64_t h = 1469598103934665603ULL;
            for (auto v : k.origin) { h ^= static_cast<std::uint32_t>(v); h *= 1099511628211ULL; }
            return static_cast<std::size_t>((h ^ k.lod) * 1099511628211ULL);
        }
    };
    std::int64_t floorDiv(std::int64_t x, std::int64_t d)
    {
        const auto q = x / d;
        return q - (x % d < 0 ? 1 : 0);
    }
    std::int64_t span(const LodChunk& c, const LodConfig& p)
    {
        return static_cast<std::int64_t>(p.cells) << c.lod;
    }
    bool relevant(const LodChunk& c, const glm::vec3& camera, const LodConfig& p)
    {
        const double low = static_cast<double>(c.origin[1]) * p.baseSpacing;
        const double high = (static_cast<double>(c.origin[1]) + span(c, p)) * p.baseSpacing;
        return high >= p.minimumY && low <= p.maximumY &&
            chunkDistanceSquared(camera, c, p.cells, p.baseSpacing) <=
                static_cast<double>(p.viewDistance) * p.viewDistance;
    }
    bool appendRecursive(const LodChunk& c, const glm::vec3& camera, const LodConfig& p,
                         std::vector<LodChunk>& leaves)
    {
        if (!relevant(c, camera, p)) return true;
        const double size = span(c, p) * static_cast<double>(p.baseSpacing);
        const double threshold = (std::max)(size * p.splitDistanceFactor,
                                           static_cast<double>(p.fullResolutionRadius));
        if (c.lod && chunkDistanceSquared(camera, c, p.cells, p.baseSpacing) <= threshold * threshold)
        {
            const auto half = span(c, p) / 2;
            for (unsigned i = 0; i < 8; ++i)
            {
                LodChunk child = c;
                --child.lod;
                for (unsigned a = 0; a < 3; ++a)
                    child.origin[a] += static_cast<std::int32_t>(((i >> a) & 1u) * half);
                if (!appendRecursive(child, camera, p, leaves)) return false;
            }
        }
        else
        {
            if (leaves.size() >= p.maxLeaves) return false;
            leaves.push_back(c);
        }
        return true;
    }
    using Index = std::unordered_map<Key, std::size_t, Hash>;
    std::size_t neighbor(const LodChunk& c, unsigned face, const LodConfig& p, const Index& index)
    {
        const auto size = span(c, p);
        std::array<std::int64_t, 3> point;
        for (unsigned a = 0; a < 3; ++a) point[a] = c.origin[a] + size / 2;
        point[face / 2] = static_cast<std::int64_t>(c.origin[face / 2]) +
            ((face & 1u) ? size : -1);
        for (std::uint32_t lod = 0; lod <= p.maxLod; ++lod)
        {
            const auto neighborSize = static_cast<std::int64_t>(p.cells) << lod;
            Key key{{}, lod};
            bool representable = true;
            for (unsigned a = 0; a < 3; ++a)
            {
                const auto v = floorDiv(point[a], neighborSize) * neighborSize;
                if (v < INT32_MIN || v > INT32_MAX) { representable = false; break; }
                key.origin[a] = static_cast<std::int32_t>(v);
            }
            if (!representable) continue;
            const auto it = index.find(key);
            if (it != index.end()) return it->second;
        }
        return static_cast<std::size_t>(-1);
    }
}

double chunkDistanceSquared(const glm::vec3& position, const LodChunk& c,
                            std::uint32_t cells, float baseSpacing) noexcept
{
    const double size = (static_cast<std::uint64_t>(cells) << c.lod) * static_cast<double>(baseSpacing);
    double squared = 0.0;
    for (unsigned a = 0; a < 3; ++a)
    {
        const double low = static_cast<double>(c.origin[a]) * baseSpacing;
        const double d = (std::max)({low - position[a], static_cast<double>(position[a]) - low - size, 0.0});
        squared += d * d;
    }
    return squared;
}

bool selectLodChunks(const glm::vec3& camera, const LodConfig& p,
                     std::vector<LodChunk>& output, std::string& error)
{
    error.clear();
    const float finite[] = {camera.x, camera.y, camera.z, p.baseSpacing, p.viewDistance,
        p.splitDistanceFactor, p.minimumY, p.maximumY, p.fullResolutionRadius};
    for (float v : finite)
        if (!finiteFieldValue(v)) { error = "Terrain LOD parameters must be finite"; return false; }
    if (p.cells < 2 || p.cells > 64 || (p.cells & (p.cells - 1u)) || p.maxLod > 12 ||
        p.baseSpacing <= 0.0f || p.viewDistance <= 0.0f || p.splitDistanceFactor <= 0.0f ||
        p.fullResolutionRadius < 0.0f || p.minimumY > p.maximumY || p.maxLeaves == 0 ||
        p.maxLeaves > 65536)
    { error = "Invalid terrain LOD configuration"; return false; }

    const std::int64_t rootSize = static_cast<std::int64_t>(p.cells) << p.maxLod;
    const double rootMetres = rootSize * static_cast<double>(p.baseSpacing);
    std::array<std::int64_t, 3> low{}, high{};
    std::uint64_t rootCount = 1;
    for (unsigned a = 0; a < 3; ++a)
    {
        double minimum = static_cast<double>(camera[a]) - p.viewDistance;
        double maximum = static_cast<double>(camera[a]) + p.viewDistance;
        if (a == 1) { minimum = (std::max)(minimum, static_cast<double>(p.minimumY));
                      maximum = (std::min)(maximum, static_cast<double>(p.maximumY)); }
        if (minimum > maximum) { output.clear(); return true; }
        const double l = std::floor(minimum / rootMetres);
        const double h = std::floor(maximum / rootMetres);
        if (l * rootSize < static_cast<double>(INT32_MIN) + rootSize ||
            (h + 1.0) * rootSize > static_cast<double>(INT32_MAX) - rootSize)
        { error = "Terrain position exceeds the supported integer lattice"; return false; }
        low[a] = static_cast<std::int64_t>(l); high[a] = static_cast<std::int64_t>(h);
        const auto count = static_cast<std::uint64_t>(high[a] - low[a] + 1);
        if (count > p.maxLeaves * 8ULL || rootCount > p.maxLeaves * 8ULL / count)
        { error = "Terrain view covers too many root chunks"; return false; }
        rootCount *= count;
    }
    std::vector<LodChunk> leaves;
    leaves.reserve((std::min)(p.maxLeaves, 1024u));
    for (auto z = low[2]; z <= high[2]; ++z)
        for (auto y = low[1]; y <= high[1]; ++y)
            for (auto x = low[0]; x <= high[0]; ++x)
            {
                LodChunk root{{static_cast<std::int32_t>(x * rootSize),
                    static_cast<std::int32_t>(y * rootSize), static_cast<std::int32_t>(z * rootSize)}, p.maxLod, 0};
                if (!appendRecursive(root, camera, p, leaves))
                { error = "Terrain selection exceeds maxSelectedChunks"; return false; }
            }

    for (;;)
    {
        Index index;
        index.reserve(leaves.size() * 2);
        for (std::size_t i = 0; i < leaves.size(); ++i)
            index.emplace(Key{leaves[i].origin, leaves[i].lod}, i);
        std::unordered_set<std::size_t> split;
        for (const auto& c : leaves)
            for (unsigned face = 0; face < 6; ++face)
            {
                const auto n = neighbor(c, face, p, index);
                if (n != static_cast<std::size_t>(-1) && leaves[n].lod > c.lod + 1)
                    split.insert(n);
            }
        if (split.empty())
        {
            for (auto& c : leaves)
                for (unsigned face = 0; face < 6; ++face)
                {
                    const auto n = neighbor(c, face, p, index);
                    if (n != static_cast<std::size_t>(-1) && leaves[n].lod == c.lod + 1)
                        c.transitionMask |= 1u << face;
                }
            break;
        }
        std::vector<LodChunk> balanced;
        balanced.reserve((std::min)(static_cast<std::size_t>(p.maxLeaves), leaves.size() + split.size() * 7));
        for (std::size_t i = 0; i < leaves.size(); ++i)
        {
            if (!split.count(i)) balanced.push_back(leaves[i]);
            else
            {
                const auto half = span(leaves[i], p) / 2;
                for (unsigned childId = 0; childId < 8; ++childId)
                {
                    auto child = leaves[i]; --child.lod;
                    for (unsigned a = 0; a < 3; ++a)
                        child.origin[a] += static_cast<std::int32_t>(((childId >> a) & 1u) * half);
                    if (relevant(child, camera, p)) balanced.push_back(child);
                }
            }
            if (balanced.size() > p.maxLeaves)
            { error = "Balanced terrain selection exceeds maxSelectedChunks"; return false; }
        }
        leaves.swap(balanced);
    }
    std::sort(leaves.begin(), leaves.end(), [&](const LodChunk& a, const LodChunk& b)
    {
        const double da = chunkDistanceSquared(camera, a, p.cells, p.baseSpacing);
        const double db = chunkDistanceSquared(camera, b, p.cells, p.baseSpacing);
        if (da != db) return da < db;
        if (a.lod != b.lod) return a.lod < b.lod;
        return a.origin < b.origin;
    });
    output.swap(leaves);
    return true;
}
}
