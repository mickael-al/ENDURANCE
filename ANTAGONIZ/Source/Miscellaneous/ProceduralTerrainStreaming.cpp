#include "ProceduralTerrainStreaming.hpp"
#include "ProceduralTerrainField.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
namespace
{
    using SpatialIndex = std::unordered_map<Key, std::size_t, KeyHash>;
    using Ancestors = std::unordered_set<Key, KeyHash>;
    constexpr std::size_t Missing = static_cast<std::size_t>(-1);

    Key spatialKey(const LodChunk& chunk) { return {chunk.origin, chunk.lod, 0}; }
    std::int64_t floorQuotient(std::int64_t value, std::int64_t divisor)
    { return value / divisor - (value % divisor < 0 ? 1 : 0); }
    LodChunk ancestor(const LodChunk& chunk, std::uint32_t lod, std::uint32_t cells)
    {
        LodChunk result{{}, lod, 0};
        const auto size = static_cast<std::int64_t>(cells) << lod;
        for (unsigned axis = 0; axis < 3; ++axis)
            result.origin[axis] = static_cast<std::int32_t>(floorQuotient(chunk.origin[axis], size) * size);
        return result;
    }
    LodChunk childOf(const LodChunk& parent, unsigned child, std::uint32_t cells)
    {
        LodChunk result = parent; --result.lod; result.transitionMask = 0;
        const auto size = static_cast<std::int64_t>(cells) << result.lod;
        for (unsigned axis = 0; axis < 3; ++axis)
            result.origin[axis] += static_cast<std::int32_t>(((child >> axis) & 1u) * size);
        return result;
    }
    bool faceAdjacent(const Box& a, const Box& b)
    {
        unsigned touching = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const auto overlap = (std::min)(a.high[axis], b.high[axis]) - (std::max)(a.low[axis], b.low[axis]);
            if (overlap < 0) return false;
            touching += overlap == 0;
        }
        return touching == 1;
    }

    class TransactionPlanner
    {
    public:
        const std::vector<LodChunk>& previous;
        const std::vector<LodChunk>& desired;
        const glm::vec3& camera;
        const LodConfig& config;
        SpatialIndex currentIndex, desiredIndex;
        Ancestors currentAncestors, desiredAncestors;
        std::vector<Box> boxes;
        bool capacityBlocked = false;

        TransactionPlanner(const std::vector<LodChunk>& old, const std::vector<LodChunk>& wanted,
                           const glm::vec3& position, const LodConfig& settings)
            : previous(old), desired(wanted), camera(position), config(settings) {}

        bool index(const std::vector<LodChunk>& chunks, SpatialIndex& lookup, Ancestors& parents,
                   std::string& error)
        {
            lookup.reserve(chunks.size() * 2); parents.reserve(chunks.size() * 3);
            for (std::size_t i = 0; i < chunks.size(); ++i)
            {
                const auto& chunk = chunks[i];
                if (chunk.lod > config.maxLod || (chunk.transitionMask & ~63u))
                { error = "Invalid terrain streaming chunk LOD/mask"; return false; }
                const auto size = static_cast<std::int64_t>(config.cells) << chunk.lod;
                for (auto coordinate : chunk.origin)
                    if (coordinate % size)
                    { error = "Unaligned terrain streaming chunk"; return false; }
                if (!lookup.emplace(spatialKey(chunk), i).second)
                { error = "Duplicate terrain streaming chunk"; return false; }
                for (auto lod = chunk.lod; lod <= config.maxLod; ++lod)
                    parents.insert(spatialKey(ancestor(chunk, lod, config.cells)));
            }
            for (const auto& chunk : chunks)
                for (auto lod = chunk.lod + 1; lod <= config.maxLod; ++lod)
                    if (lookup.count(spatialKey(ancestor(chunk, lod, config.cells))))
                    { error = "Overlapping terrain streaming chunks"; return false; }
            return true;
        }
        std::size_t covering(const LodChunk& node, const SpatialIndex& lookup) const
        {
            for (auto lod = node.lod; lod <= config.maxLod; ++lod)
            {
                const auto found = lookup.find(spatialKey(ancestor(node, lod, config.cells)));
                if (found != lookup.end()) return found->second;
            }
            return Missing;
        }
        bool wanted(const LodChunk& node) const
        { return desiredAncestors.count(spatialKey(node)) || covering(node, desiredIndex) != Missing; }
        double distance(const LodChunk& node) const
        { return chunkDistanceSquared(camera, node, config.cells, config.baseSpacing); }
        void missing(const LodChunk& node, std::vector<LodChunk>& result) const
        {
            if (covering(node, currentIndex) != Missing) return;
            if (!currentAncestors.count(spatialKey(node))) { result.push_back(node); return; }
            if (node.lod)
                for (unsigned child = 0; child < 8; ++child) missing(childOf(node, child, config.cells), result);
        }
        std::uint32_t mask(const LodChunk& node, const SpatialIndex& nextIndex) const
        {
            if (node.lod >= config.maxLod) return 0;
            const auto size = static_cast<std::int64_t>(config.cells) << node.lod;
            const auto coarseSize = size * 2;
            std::uint32_t result = 0;
            for (unsigned face = 0; face < 6; ++face)
            {
                Key neighbor{{}, node.lod + 1, 0}; bool representable = true;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const auto point = static_cast<std::int64_t>(node.origin[axis]) +
                        (axis != face / 2 ? size / 2 : ((face & 1u) ? size : -1));
                    const auto origin = floorQuotient(point, coarseSize) * coarseSize;
                    if (origin < INT32_MIN || origin > INT32_MAX) { representable = false; break; }
                    neighbor.origin[axis] = static_cast<std::int32_t>(origin);
                }
                if (representable && nextIndex.count(neighbor)) result |= 1u << face;
            }
            return result;
        }
        bool edit(const std::vector<std::size_t>& removed, const std::vector<LodChunk>& added,
                  const Box& envelope, LodWorkKind kind, LodTransaction& result,
                  const std::vector<Box>* fillBounds = nullptr)
        {
            const auto count = previous.size() - removed.size() + added.size();
            if (count > config.maxLeaves) { capacityBlocked = true; return false; }
            LodTransaction transaction; transaction.kind = kind;
            transaction.next.reserve(count);
            std::vector<bool> erase(previous.size(), false);
            for (auto i : removed) erase[i] = true;
            for (std::size_t i = 0; i < previous.size(); ++i)
                if (!erase[i]) transaction.next.push_back(previous[i]);
            const auto oldCount = transaction.next.size();
            transaction.next.insert(transaction.next.end(), added.begin(), added.end());
            SpatialIndex lookup; lookup.reserve(count * 2);
            for (std::size_t i = 0; i < transaction.next.size(); ++i)
                lookup.emplace(spatialKey(transaction.next[i]), i);
            // Only the edit's exterior neighbours can change their transition
            // ownership. Do not rebuild every boundary chunk or all masks.
            for (std::size_t i = 0; i < transaction.next.size(); ++i)
            {
                auto& node = transaction.next[i];
                bool affected = i >= oldCount;
                if (!affected)
                {
                    const auto box = boxOf(node, config.cells);
                    if (fillBounds)
                        for (const auto& changed : *fillBounds)
                            if (faceAdjacent(box, changed)) { affected = true; break; }
                    if (!fillBounds) affected = faceAdjacent(box, envelope);
                }
                if (affected) node.transitionMask = mask(node, lookup);
            }
            std::vector<bool> retained(previous.size(), false);
            for (std::size_t i = 0; i < transaction.next.size(); ++i)
            {
                const auto& node = transaction.next[i];
                const auto found = currentIndex.find(spatialKey(node));
                if (found != currentIndex.end() && previous[found->second].transitionMask == node.transitionMask)
                    retained[found->second] = true;
                else transaction.nextIndices.push_back(i);
            }
            for (std::size_t i = 0; i < previous.size(); ++i)
                if (!retained[i]) transaction.previousIndices.push_back(i);
            // A single split has eight children and at most four fine neighbours
            // on each of six faces; merges/additions are smaller.
            if (transaction.nextIndices.size() > 32) return false;
            std::sort(transaction.nextIndices.begin(), transaction.nextIndices.end(),
                [&](std::size_t a, std::size_t b)
                {
                    const auto& x = transaction.next[a]; const auto& y = transaction.next[b];
                    if (x.lod != y.lod) return x.lod < y.lod;
                    const auto dx = distance(x), dy = distance(y);
                    return dx != dy ? dx < dy : x.origin < y.origin;
                });
            result = std::move(transaction); return true;
        }
        bool split(std::size_t source, LodTransaction& result)
        {
            const auto& node = previous[source];
            if (!node.lod) return false;
            std::vector<LodChunk> children;
            for (unsigned i = 0; i < 8; ++i)
            {
                auto child = childOf(node, i, config.cells);
                if (wanted(child)) children.push_back(child);
            }
            if (children.empty()) return false; // Free retirement is handled first.
            // A coarser neighbour is a refinement prerequisite. Recursion moves
            // strictly up in LOD and therefore cannot restart an unfinished wave.
            for (std::size_t other = 0; other < previous.size(); ++other)
            {
                if (other == source || previous[other].lod <= node.lod) continue;
                for (const auto& child : children)
                    if (faceAdjacent(boxOf(child, config.cells), boxes[other])) return split(other, result);
            }
            return edit({source}, children, boxes[source], LodWorkKind::Refine, result);
        }
        bool fill(const LodChunk& node, LodTransaction& result)
        {
            const auto box = boxOf(node, config.cells);
            bool needsSmaller = false;
            for (std::size_t i = 0; i < previous.size(); ++i)
            {
                if (!faceAdjacent(box, boxes[i])) continue;
                if (previous[i].lod > node.lod + 1) return split(i, result);
                if (node.lod > previous[i].lod + 1) needsSmaller = true;
            }
            if (!needsSmaller) return edit({}, {node}, box, LodWorkKind::Fill, result);
            if (!node.lod) return false;
            std::vector<LodChunk> children;
            for (unsigned i = 0; i < 8; ++i) children.push_back(childOf(node, i, config.cells));
            std::sort(children.begin(), children.end(), [&](const LodChunk& a, const LodChunk& b)
            { const auto da = distance(a), db = distance(b); return da != db ? da < db : a.origin < b.origin; });
            return fill(children.front(), result);
        }
        bool fillBatch(const std::vector<LodChunk>& holes, LodTransaction& result)
        {
            // Four direct additions keep both GPU slots busy during initial
            // coverage. Stop at the first prerequisite instead of skipping a
            // higher-priority hole. Balance-driven splits remain local edits.
            std::vector<LodChunk> added;
            std::vector<Box> changed;
            for (const auto& node : holes)
            {
                if (added.size() == 4 || previous.size() + added.size() == config.maxLeaves) break;
                const auto box = boxOf(node, config.cells);
                bool direct = true;
                for (std::size_t i = 0; i < previous.size(); ++i)
                    if ((node.lod + 1 < previous[i].lod || previous[i].lod + 1 < node.lod) &&
                        faceAdjacent(box, boxes[i])) { direct = false; break; }
                for (std::size_t i = 0; direct && i < added.size(); ++i)
                    if ((node.lod + 1 < added[i].lod || added[i].lod + 1 < node.lod) &&
                        faceAdjacent(box, changed[i])) direct = false;
                if (!direct) break;
                added.push_back(node); changed.push_back(box);
            }
            while (!added.empty())
            {
                if (edit({}, added, changed.front(), LodWorkKind::Fill, result, &changed)) return true;
                // A dense frontier may change too many existing transition
                // meshes. Reduce the batch, preserving the global 32-job bound.
                added.pop_back(); changed.pop_back();
            }
            return fill(holes.front(), result);
        }
        bool descendantTouchesFace(const LodChunk& node, unsigned face) const
        {
            const auto key = spatialKey(node);
            if (currentIndex.count(key)) return true;
            if (!node.lod || !currentAncestors.count(key)) return false;
            for (unsigned child = 0; child < 8; ++child)
                if (((child >> (face / 2)) & 1u) == (face & 1u) &&
                    descendantTouchesFace(childOf(node, child, config.cells), face)) return true;
            return false;
        }
        bool hasFinerNeighbour(const LodChunk& parent) const
        {
            if (parent.lod < 2) return false;
            const auto half = static_cast<std::int64_t>(config.cells) << (parent.lod - 1);
            // Query the four half-size cubes across each face through the octree
            // index. A whole-selection scan for every merge candidate becomes
            // quadratic precisely when many distant merges are still blocked.
            for (unsigned face = 0; face < 6; ++face)
                for (unsigned tile = 0; tile < 4; ++tile)
                {
                    LodChunk neighbour{{}, parent.lod - 1, 0};
                    unsigned tileAxis = 0; bool representable = true;
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        const auto offset = axis == face / 2 ? ((face & 1u) ? 2 * half : -half)
                            : static_cast<std::int64_t>((tile >> tileAxis++) & 1u) * half;
                        const auto coordinate = static_cast<std::int64_t>(parent.origin[axis]) + offset;
                        if (coordinate < INT32_MIN || coordinate > INT32_MAX) { representable = false; break; }
                        neighbour.origin[axis] = static_cast<std::int32_t>(coordinate);
                    }
                    if (!representable || currentIndex.count(spatialKey(neighbour))) continue;
                    if (descendantTouchesFace(neighbour, face ^ 1u)) return true;
                }
            return false;
        }
        bool coarsen(LodTransaction& result)
        {
            std::vector<LodChunk> candidates;
            Ancestors seen;
            for (const auto& chunk : previous)
            {
                if (chunk.lod >= config.maxLod) continue;
                const auto parent = ancestor(chunk, chunk.lod + 1, config.cells);
                if (covering(parent, desiredIndex) != Missing && seen.insert(spatialKey(parent)).second)
                    candidates.push_back(parent);
            }
            std::sort(candidates.begin(), candidates.end(), [&](const LodChunk& a, const LodChunk& b)
            {
                if (a.lod != b.lod) return a.lod < b.lod;
                const auto da = distance(a), db = distance(b);
                return da != db ? da > db : a.origin < b.origin;
            });
            for (const auto& parent : candidates)
            {
                std::vector<std::size_t> removed;
                bool blocked = false;
                for (unsigned i = 0; i < 8; ++i)
                {
                    const auto child = childOf(parent, i, config.cells);
                    const auto found = currentIndex.find(spatialKey(child));
                    if (found != currentIndex.end()) removed.push_back(found->second);
                    else if (currentAncestors.count(spatialKey(child))) { blocked = true; break; }
                }
                if (blocked || removed.empty()) continue;
                const auto box = boxOf(parent, config.cells);
                if (!hasFinerNeighbour(parent) && edit(removed, {parent}, box, LodWorkKind::Coarsen, result)) return true;
            }
            return false;
        }
        bool plan(LodTransaction& result, std::string& error)
        {
            if (!index(previous, currentIndex, currentAncestors, error) ||
                !index(desired, desiredIndex, desiredAncestors, error)) return false;
            boxes.reserve(previous.size());
            LodTransaction retirement; retirement.kind = LodWorkKind::Retire;
            retirement.next.reserve(previous.size());
            for (std::size_t i = 0; i < previous.size(); ++i)
            {
                boxes.push_back(boxOf(previous[i], config.cells));
                if (wanted(previous[i])) retirement.next.push_back(previous[i]);
                else retirement.previousIndices.push_back(i);
            }
            if (!retirement.previousIndices.empty()) { result = std::move(retirement); return true; }

            std::vector<LodChunk> holes;
            for (const auto& chunk : desired) missing(chunk, holes);
            std::sort(holes.begin(), holes.end(), [&](const LodChunk& a, const LodChunk& b)
            {
                if (a.lod != b.lod) return a.lod < b.lod;
                const auto da = distance(a), db = distance(b);
                return da != db ? da < db : a.origin < b.origin;
            });
            if (!holes.empty() && fillBatch(holes, result)) return true;
            if (holes.empty())
            {
                std::vector<std::size_t> refine;
                for (std::size_t i = 0; i < previous.size(); ++i)
                    if (previous[i].lod && desiredAncestors.count(spatialKey(previous[i])) &&
                        covering(previous[i], desiredIndex) == Missing) refine.push_back(i);
                std::sort(refine.begin(), refine.end(), [&](std::size_t a, std::size_t b)
                {
                    if (previous[a].lod != previous[b].lod) return previous[a].lod > previous[b].lod;
                    const auto da = distance(previous[a]), db = distance(previous[b]);
                    return da != db ? da < db : previous[a].origin < previous[b].origin;
                });
                if (!refine.empty() && split(refine.front(), result)) return true;
            }
            if (coarsen(result)) { result.budgetFallback = capacityBlocked; return true; }
            if (capacityBlocked)
            { error = "Terrain LOD streaming cannot progress within maxSelectedChunks"; return false; }
            // Spatial convergence may leave stale transition bits only on empty
            // exterior faces. They are harmless and need no GPU regeneration.
            bool equal = previous.size() == desired.size();
            if (equal)
                for (const auto& chunk : previous)
                    if (!desiredIndex.count(spatialKey(chunk))) { equal = false; break; }
            if (!equal)
            { error = "Terrain LOD streaming has no compatible local transaction"; return false; }
            result = {}; result.next = previous; return true;
        }
    };
}

bool planLodTransaction(const std::vector<LodChunk>& previous,
                        const std::vector<LodChunk>& desired,
                        const glm::vec3& camera, const LodConfig& config,
                        LodTransaction& output, std::string& error)
{
    error.clear();
    if (config.cells < 2 || config.cells > 64 || (config.cells & (config.cells - 1u)) ||
        config.maxLod > 12 || !config.maxLeaves || config.maxLeaves > 65536 ||
        !finiteFieldValue(config.baseSpacing) || config.baseSpacing <= 0.0f ||
        !finiteFieldValue(camera.x) || !finiteFieldValue(camera.y) || !finiteFieldValue(camera.z))
    { error = "Invalid terrain streaming configuration"; return false; }
    TransactionPlanner planner(previous, desired, camera, config);
    LodTransaction transaction;
    if (!planner.plan(transaction, error)) return false;
    output = std::move(transaction); return true;
}
}
