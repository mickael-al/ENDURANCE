#include "ProceduralTerrainStreaming.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace ProceduralTerrain;
namespace
{
constexpr std::uint32_t Cells = 8;
using Position = std::array<std::int64_t, 3>;
using ChunkId = std::tuple<std::array<std::int32_t, 3>, std::uint32_t, std::uint32_t>;
using Coverage = std::set<Position>;

void require(bool condition, const std::string& message)
{
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
ChunkId id(const LodChunk& chunk)
{ return {chunk.origin, chunk.lod, chunk.transitionMask}; }

// Independent geometric oracle: positive overlap in all axes is a collision;
// two positive overlaps and one exact boundary is an actual shared face.
int sharedFace(const LodChunk& a, const LodChunk& b)
{
    const std::int64_t as = static_cast<std::int64_t>(Cells) << a.lod;
    const std::int64_t bs = static_cast<std::int64_t>(Cells) << b.lod;
    int face = -1, touching = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto distance = (std::min)(static_cast<std::int64_t>(a.origin[axis]) + as,
                                        static_cast<std::int64_t>(b.origin[axis]) + bs)
                            - (std::max)(static_cast<std::int64_t>(a.origin[axis]),
                                         static_cast<std::int64_t>(b.origin[axis]));
        if (distance < 0) return -1;
        if (distance == 0)
        {
            ++touching;
            face = 2 * axis + (a.origin[axis] < b.origin[axis] ? 1 : 0);
        }
    }
    require(touching != 0, "mixed selection contains overlapping chunk volumes");
    return touching == 1 ? face : -1;
}

void deriveMasks(std::vector<LodChunk>& chunks)
{
    for (auto& chunk : chunks) chunk.transitionMask = 0;
    for (std::size_t a = 0; a < chunks.size(); ++a)
        for (std::size_t b = a + 1; b < chunks.size(); ++b)
        {
            const int face = sharedFace(chunks[a], chunks[b]);
            if (face < 0) continue;
            require(std::abs(static_cast<int>(chunks[a].lod) - static_cast<int>(chunks[b].lod)) <= 1,
                    "test fixture is unbalanced");
            if (chunks[a].lod < chunks[b].lod) chunks[a].transitionMask |= 1u << face;
            if (chunks[b].lod < chunks[a].lod) chunks[b].transitionMask |= 1u << (face ^ 1);
        }
}

void verifyTopology(const std::vector<LodChunk>& chunks)
{
    for (std::size_t a = 0; a < chunks.size(); ++a)
        for (std::size_t b = a + 1; b < chunks.size(); ++b)
        {
            const int face = sharedFace(chunks[a], chunks[b]);
            if (face < 0) continue;
            require(std::abs(static_cast<int>(chunks[a].lod) - static_cast<int>(chunks[b].lod)) <= 1,
                    "mixed selection violates face balance");
            require(((chunks[a].transitionMask >> face) & 1u) == (chunks[a].lod < chunks[b].lod),
                    "first chunk has an incompatible transition mask");
            require(((chunks[b].transitionMask >> (face ^ 1)) & 1u) == (chunks[b].lod < chunks[a].lod),
                    "second chunk has an incompatible transition mask");
        }
}

Coverage coverageOf(const std::vector<LodChunk>& chunks)
{
    Coverage result;
    for (const auto& chunk : chunks)
    {
        const auto span = std::int64_t{1} << chunk.lod;
        for (std::int64_t z = 0; z < span; ++z)
            for (std::int64_t y = 0; y < span; ++y)
                for (std::int64_t x = 0; x < span; ++x)
                {
                    // Cast before division: negative origins must stay signed.
                    Position cell{{static_cast<std::int64_t>(chunk.origin[0]) / Cells + x,
                             static_cast<std::int64_t>(chunk.origin[1]) / Cells + y,
                             static_cast<std::int64_t>(chunk.origin[2]) / Cells + z}};
                    require(result.insert(cell).second, "coverage contains a duplicate base cell");
                }
    }
    return result;
}

void appendChildren(std::vector<LodChunk>& chunks, const LodChunk& parent)
{
    require(parent.lod > 0, "cannot split LOD0 fixture");
    const auto half = Cells << (parent.lod - 1u);
    for (unsigned child = 0; child < 8; ++child)
    {
        auto chunk = parent;
        --chunk.lod; chunk.transitionMask = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            chunk.origin[axis] += static_cast<std::int32_t>(((child >> axis) & 1u) * half);
        chunks.push_back(chunk);
    }
}

std::size_t check(const std::string& name, std::vector<LodChunk> previous,
                  std::vector<LodChunk> next, bool exhaustive = true)
{
    deriveMasks(previous); deriveMasks(next);
    const auto groups = groupLodChanges(previous, next, Cells);
    require(!exhaustive || groups.size() <= 16, "fixture has too many groups for exhaustive checking");
    std::set<ChunkId> oldKeys, newKeys;
    for (const auto& chunk : previous) oldKeys.insert(id(chunk));
    for (const auto& chunk : next) newKeys.insert(id(chunk));
    std::vector<int> oldGroup(previous.size(), -1), newGroup(next.size(), -1);
    bool removalOnly = false;
    std::size_t lastNewIndex = 0;
    for (std::size_t g = 0; g < groups.size(); ++g)
    {
        const auto& group = groups[g];
        require(!group.previousIndices.empty() || !group.nextIndices.empty(), "empty change group");
        require(std::is_sorted(group.previousIndices.begin(), group.previousIndices.end()) &&
                std::is_sorted(group.nextIndices.begin(), group.nextIndices.end()), "unstable input ordering");
        if (group.nextIndices.empty()) removalOnly = true;
        else
        {
            require(!removalOnly && (g == 0 || group.nextIndices.front() > lastNewIndex), "unstable group priority");
            lastNewIndex = group.nextIndices.front();
        }
        for (auto index : group.previousIndices)
        {
            require(index < previous.size() && oldGroup[index] == -1, "duplicate/invalid previous index");
            oldGroup[index] = static_cast<int>(g);
        }
        for (auto index : group.nextIndices)
        {
            require(index < next.size() && newGroup[index] == -1, "duplicate/invalid next index");
            newGroup[index] = static_cast<int>(g);
        }
    }
    for (std::size_t i = 0; i < previous.size(); ++i)
        require((oldGroup[i] == -1) == (newKeys.count(id(previous[i])) != 0), "old change classification");
    for (std::size_t i = 0; i < next.size(); ++i)
        require((newGroup[i] == -1) == (oldKeys.count(id(next[i])) != 0), "new change classification");

    const auto oldCoverage = coverageOf(previous), newCoverage = coverageOf(next);
    Coverage retained, allowed = oldCoverage;
    allowed.insert(newCoverage.begin(), newCoverage.end());
    std::set_intersection(oldCoverage.begin(), oldCoverage.end(), newCoverage.begin(), newCoverage.end(),
                          std::inserter(retained, retained.end()));
    const std::size_t combinations = exhaustive ? (std::size_t{1} << groups.size()) : 128;
    std::uint32_t random = 0x9147abcdu;
    for (std::size_t combination = 0; combination < combinations; ++combination)
    {
        std::vector<bool> published(groups.size());
        for (std::size_t g = 0; g < groups.size(); ++g)
        {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            published[g] = exhaustive ? ((combination >> g) & 1u) != 0 : (random & 1u) != 0;
        }
        std::vector<LodChunk> mixed;
        for (std::size_t i = 0; i < previous.size(); ++i)
            if (oldGroup[i] == -1 || !published[oldGroup[i]]) mixed.push_back(previous[i]);
        for (std::size_t i = 0; i < next.size(); ++i)
            if (newGroup[i] != -1 && published[newGroup[i]]) mixed.push_back(next[i]);
        verifyTopology(mixed);
        const auto coverage = coverageOf(mixed);
        require(std::includes(coverage.begin(), coverage.end(), retained.begin(), retained.end()),
                "partial publication opens a hole in retained coverage");
        require(std::includes(allowed.begin(), allowed.end(), coverage.begin(), coverage.end()),
                "partial publication adds unexpected coverage");
    }
    std::cout << "PASS " << name << ": " << groups.size() << " groups, " << combinations
              << (exhaustive ? " exhaustive" : " sampled") << " combinations\n";
    return groups.size();
}

void checkMovingSelection()
{
    LodConfig config;
    config.cells = Cells; config.maxLod = 3; config.viewDistance = 48;
    config.splitDistanceFactor = 0.75f; config.fullResolutionRadius = 4;
    config.minimumY = -16; config.maximumY = 16; config.maxLeaves = 1024;
    std::vector<LodChunk> current, desired;
    std::string error;
    require(selectLodChunks(glm::vec3(1, 2, 3), config, current, error), error);
    std::set<ChunkId> ready;
    std::map<ChunkId, unsigned> pending;
    for (const auto& chunk : current) ready.insert(id(chunk));
    unsigned commitsDuringMovement = 0, commitsBeforeWholeTarget = 0;

    // Six camera positions, two in-flight jobs and asynchronous two-tick
    // completions. Retargeting retains ready/pending keys still wanted by the
    // latest selection, as the streaming integration does. Finish at rest to
    // distinguish finite GPU throughput from cancellation-induced starvation.
    for (unsigned tick = 0; tick < 640; ++tick)
    {
        if (tick < 384 && tick % 64 == 0)
        {
            const glm::vec3 position(9.0f + 8.0f * (tick / 64), 2.0f, 3.0f);
            require(selectLodChunks(position, config, desired, error), error);
            std::set<ChunkId> wanted;
            for (const auto& chunk : desired) wanted.insert(id(chunk));
            for (auto it = pending.begin(); it != pending.end();)
                if (!wanted.count(it->first)) it = pending.erase(it); else ++it;
        }
        for (auto it = pending.begin(); it != pending.end();)
            if (it->second <= tick) { ready.insert(it->first); it = pending.erase(it); }
            else ++it;

        const bool wholeTargetReady = std::all_of(desired.begin(), desired.end(),
            [&](const LodChunk& chunk) { return ready.count(id(chunk)) != 0; });
        const auto groups = groupLodChanges(current, desired, Cells);
        std::vector<bool> replaced(current.size(), false);
        std::vector<LodChunk> additions;
        unsigned published = 0;
        for (const auto& group : groups)
        {
            if (!std::all_of(group.nextIndices.begin(), group.nextIndices.end(),
                [&](std::size_t i) { return ready.count(id(desired[i])) != 0; })) continue;
            for (auto i : group.previousIndices) replaced[i] = true;
            for (auto i : group.nextIndices) additions.push_back(desired[i]);
            ++published;
        }
        if (published)
        {
            std::vector<LodChunk> mixed;
            for (std::size_t i = 0; i < current.size(); ++i)
                if (!replaced[i]) mixed.push_back(current[i]);
            mixed.insert(mixed.end(), additions.begin(), additions.end());
            verifyTopology(mixed);
            const auto before = coverageOf(current), target = coverageOf(desired), after = coverageOf(mixed);
            Coverage retained;
            std::set_intersection(before.begin(), before.end(), target.begin(), target.end(),
                                  std::inserter(retained, retained.end()));
            require(std::includes(after.begin(), after.end(), retained.begin(), retained.end()),
                    "moving publication opened a coverage hole");
            current.swap(mixed);
            if (tick < 384) commitsDuringMovement += published;
            if (!wholeTargetReady) commitsBeforeWholeTarget += published;
        }
        for (const auto& chunk : desired)
        {
            if (pending.size() == 2) break;
            const auto key = id(chunk);
            if (!ready.count(key) && !pending.count(key)) pending.emplace(key, tick + 2);
        }
        std::set<ChunkId> retainedKeys;
        for (const auto& chunk : current) retainedKeys.insert(id(chunk));
        for (const auto& chunk : desired) retainedKeys.insert(id(chunk));
        for (auto it = ready.begin(); it != ready.end();)
            if (!retainedKeys.count(*it)) it = ready.erase(it); else ++it;
    }
    require(commitsDuringMovement > 6, "streaming made insufficient progress during camera motion");
    require(commitsBeforeWholeTarget > 0, "local groups still wait for the entire target");
    require(groupLodChanges(current, desired, Cells).empty(), "streaming never converged after camera stopped");
    std::cout << "PASS moving selection: " << commitsDuringMovement << " group commits during motion, "
              << commitsBeforeWholeTarget << " before complete target; converged after stopping\n";
}
}

int main()
{
    const LodChunk a{{0, 0, 0}, 1, 0}, b{{16, 0, 0}, 1, 0}, distant{{64, 0, 0}, 1, 0};
    require(check("unchanged", {a, b}, {a, b}) == 0, "unchanged chunks should be reused");
    std::vector<LodChunk> splitA, splitB, splitDistant;
    appendChildren(splitA, a); appendChildren(splitB, b); appendChildren(splitDistant, distant);
    auto independent = splitA; independent.insert(independent.end(), splitDistant.begin(), splitDistant.end());
    require(check("independent splits", {a, distant}, independent) == 2,
            "distant changes must publish independently");
    auto fineBesideCoarse = splitA; fineBesideCoarse.push_back(b);
    require(check("split next to reused coarse neighbour", {a, b}, fineBesideCoarse) == 1,
            "coarse neighbour should not require regeneration");
    auto opposite = splitB; opposite.push_back(a);
    require(check("moving fine region across shared face", fineBesideCoarse, opposite) == 1,
            "incompatible equal-LOD transition faces must commit together");
    auto adjacentFine = splitA; adjacentFine.insert(adjacentFine.end(), splitB.begin(), splitB.end());
    require(check("adjacent simultaneous splits", {a, b}, adjacentFine) == 1,
            "missing fine transition must group neighbouring splits");
    require(check("adjacent simultaneous merges", adjacentFine, {a, b}) == 1,
            "missing fine transition must group neighbouring merges");
    for (unsigned axis = 0; axis < 3; ++axis)
        for (int direction : {-1, 1})
        {
            auto before = fineBesideCoarse, after = opposite;
            for (auto* chunks : {&before, &after})
                for (auto& chunk : *chunks)
                {
                    std::swap(chunk.origin[0], chunk.origin[axis]);
                    if (direction < 0)
                        chunk.origin[axis] = -chunk.origin[axis] - static_cast<std::int32_t>(Cells << chunk.lod);
                }
            require(check("shared face " + std::to_string(2 * axis + (direction > 0)), before, after) == 1,
                    "face orientation changed grouping");
        }
    const LodChunk edgeNeighbour{{16, 16, 0}, 1, 0};
    auto edgeSplit = splitA;
    appendChildren(edgeSplit, edgeNeighbour);
    require(check("edge contact remains independent", {a, edgeNeighbour}, edgeSplit) == 2,
            "edge-only contact should not serialize independent changes");
    std::vector<LodChunk> levelOne, levelZero;
    const LodChunk largeA{{0, 0, 0}, 2, 0}, largeB{{32, 0, 0}, 2, 0};
    appendChildren(levelOne, largeA); appendChildren(levelOne, largeB);
    for (const auto& chunk : levelOne) appendChildren(levelZero, chunk);
    require(check("mixed LOD gap greater than one", levelZero, {largeA, largeB}) == 1,
            "a mixed two-level gap must never cross change groups");
    require(check("view boundary add/remove", {a, b}, {b, distant}) == 2,
            "unrelated view-boundary changes should be independent");
    require(check("transition neighbour leaves coverage", {{{8, 0, 0}, 0, 0}, b}, {{{8, 0, 0}, 0, 0}}) == 1,
            "mask update must wait for the removed coarse neighbour");

    auto negativeOld = std::vector<LodChunk>{a, distant};
    auto negativeNext = independent;
    for (auto& chunk : negativeOld) for (auto& coordinate : chunk.origin) coordinate -= 128;
    for (auto& chunk : negativeNext) for (auto& coordinate : chunk.origin) coordinate -= 128;
    require(check("negative coordinates", negativeOld, negativeNext) == 2,
            "negative coordinates changed independent grouping");

    LodConfig config;
    config.cells = Cells; config.maxLod = 3; config.viewDistance = 48;
    config.splitDistanceFactor = 0.75f; config.fullResolutionRadius = 4;
    config.minimumY = -16; config.maximumY = 16; config.maxLeaves = 1024;
    std::string error;
    std::vector<LodChunk> previous, next;
    require(selectLodChunks(glm::vec3(1, 2, 3), config, previous, error), error);
    for (const auto position : {glm::vec3(9, 2, 3), glm::vec3(9, 10, -5), glm::vec3(-19, -2, -27)})
    {
        require(selectLodChunks(position, config, next, error), error);
        check("production LOD movement", previous, next, false);
        previous = next;
    }
    checkMovingSelection();
}
