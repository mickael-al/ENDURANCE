#include "ProceduralTerrainStreaming.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <unordered_map>

namespace ProceduralTerrain
{
namespace
{
    struct Key
    {
        std::array<std::int32_t, 3> origin;
        std::uint32_t lod, mask;
        bool operator==(const Key& other) const noexcept
        { return origin == other.origin && lod == other.lod && mask == other.mask; }
    };
    struct KeyHash
    {
        std::size_t operator()(const Key& key) const noexcept
        {
            std::size_t hash = 2166136261u;
            for (auto coordinate : key.origin)
                hash = (hash ^ static_cast<std::uint32_t>(coordinate)) * 16777619u;
            return ((hash ^ key.lod) * 16777619u ^ key.mask) * 16777619u;
        }
    };
    Key keyOf(const LodChunk& chunk)
    { return {chunk.origin, chunk.lod, chunk.transitionMask}; }

    struct Box
    {
        std::array<std::int64_t, 3> low, high;
    };
    Box boxOf(const LodChunk& chunk, std::uint32_t cells)
    {
        Box result;
        const auto span = static_cast<std::int64_t>(cells) << chunk.lod;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            result.low[axis] = chunk.origin[axis];
            result.high[axis] = result.low[axis] + span;
        }
        return result;
    }

    bool mustCommitTogether(const LodChunk& oldChunk, const Box& oldBox,
                            const LodChunk& newChunk, const Box& newBox)
    {
        unsigned separated = 0, touchingAxis = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            if (oldBox.high[axis] < newBox.low[axis] || newBox.high[axis] < oldBox.low[axis])
                return false;
            if (oldBox.high[axis] == newBox.low[axis] || newBox.high[axis] == oldBox.low[axis])
            {
                ++separated;
                touchingAxis = axis;
            }
        }
        if (separated == 0) return true; // Mutually exclusive volume coverage.
        if (separated != 1) return false; // Edge/corner contact has no transition face.

        const unsigned oldFace = touchingAxis * 2u +
            (oldBox.high[touchingAxis] == newBox.low[touchingAxis] ? 1u : 0u);
        const unsigned newFace = oldFace ^ 1u;
        const bool oldTransition = (oldChunk.transitionMask & (1u << oldFace)) != 0;
        const bool newTransition = (newChunk.transitionMask & (1u << newFace)) != 0;
        if (oldChunk.lod == newChunk.lod) return oldTransition || newTransition;
        if (oldChunk.lod < newChunk.lod)
            return newChunk.lod - oldChunk.lod != 1u || !oldTransition || newTransition;
        return oldChunk.lod - newChunk.lod != 1u || oldTransition || !newTransition;
    }

    struct DisjointSets
    {
        std::vector<std::size_t> parent;
        explicit DisjointSets(std::size_t count) : parent(count)
        { std::iota(parent.begin(), parent.end(), std::size_t{0}); }
        std::size_t root(std::size_t index)
        {
            while (parent[index] != index)
            {
                parent[index] = parent[parent[index]];
                index = parent[index];
            }
            return index;
        }
        void unite(std::size_t a, std::size_t b)
        {
            a = root(a); b = root(b);
            if (a != b) parent[b] = a;
        }
    };
}

std::vector<LodChangeGroup> groupLodChanges(const std::vector<LodChunk>& previous,
                                          const std::vector<LodChunk>& next,
                                          std::uint32_t cells)
{
    std::unordered_map<Key, std::size_t, KeyHash> oldIndex;
    oldIndex.reserve(previous.size());
    for (std::size_t i = 0; i < previous.size(); ++i) oldIndex.emplace(keyOf(previous[i]), i);
    std::vector<bool> unchanged(previous.size(), false);
    std::vector<std::size_t> oldChanged, newChanged;
    newChanged.reserve(next.size());
    for (std::size_t i = 0; i < next.size(); ++i)
    {
        const auto found = oldIndex.find(keyOf(next[i]));
        if (found == oldIndex.end()) newChanged.push_back(i);
        else unchanged[found->second] = true;
    }
    oldChanged.reserve(previous.size());
    for (std::size_t i = 0; i < previous.size(); ++i)
        if (!unchanged[i]) oldChanged.push_back(i);

    std::vector<Box> oldBoxes, newBoxes;
    oldBoxes.reserve(oldChanged.size()); newBoxes.reserve(newChanged.size());
    for (auto i : oldChanged) oldBoxes.push_back(boxOf(previous[i], cells));
    for (auto i : newChanged) newBoxes.push_back(boxOf(next[i], cells));
    const auto newOffset = oldChanged.size();
    DisjointSets sets(newOffset + newChanged.size());
    for (std::size_t old = 0; old < oldChanged.size(); ++old)
        for (std::size_t current = 0; current < newChanged.size(); ++current)
            if (mustCommitTogether(previous[oldChanged[old]], oldBoxes[old],
                                   next[newChanged[current]], newBoxes[current]))
                sets.unite(old, newOffset + current);

    std::vector<LodChangeGroup> result;
    std::unordered_map<std::size_t, std::size_t> groupIndex;
    const auto groupFor = [&](std::size_t node) -> LodChangeGroup&
    {
        const auto root = sets.root(node);
        const auto inserted = groupIndex.emplace(root, result.size());
        if (inserted.second) result.emplace_back();
        return result[inserted.first->second];
    };
    // New indices establish deterministic scheduling priority, independent of
    // unordered-map iteration and the disjoint-set representative chosen.
    for (std::size_t i = 0; i < newChanged.size(); ++i)
        groupFor(newOffset + i).nextIndices.push_back(newChanged[i]);
    for (std::size_t i = 0; i < oldChanged.size(); ++i)
        groupFor(i).previousIndices.push_back(oldChanged[i]);
    return result;
}
}
