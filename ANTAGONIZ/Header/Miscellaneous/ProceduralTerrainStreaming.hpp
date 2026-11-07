#ifndef ENDURANCE_PROCEDURAL_TERRAIN_STREAMING_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_STREAMING_HPP

#include "ProceduralTerrainLod.hpp"
#include <cstddef>
#include <vector>

namespace ProceduralTerrain
{
    enum class LodWorkKind { None, Retire, Fill, Refine, Coarsen };

    struct LodTransaction
    {
        LodWorkKind kind = LodWorkKind::None;
        // A capacity prerequisite may coarsen before the preferred GPU task.
        bool budgetFallback = false;
        std::vector<LodChunk> next;
        std::vector<std::size_t> previousIndices;
        std::vector<std::size_t> nextIndices;
    };

    // Pure CPU planning. One split/merge, or up to four direct fills, plus affected
    // transition neighbours,
    // with at most 32 new meshes. Freeze this transaction until all nextIndices
    // are ready, then atomically replace previousIndices. Replan from the
    // published result and the latest desired selection after that commit.
    //
    // Out-of-range retirement is free housekeeping (no new meshes), then GPU
    // priorities are Fill fine->coarse, Refine coarse->fine, Coarsen fine->coarse.
    // A required balance or capacity prerequisite may run first. Missing
    // neighbours at the coverage boundary do not require clearing their bits.
    // True + kind None means convergence. False leaves output unchanged and
    // reports invalid input or a capacity block; it must not silently spin.
    bool planLodTransaction(const std::vector<LodChunk>& previous,
                            const std::vector<LodChunk>& desired,
                            const glm::vec3& camera, const LodConfig& config,
                            LodTransaction& output, std::string& error);

    struct LodChangeGroup
    {
        std::vector<std::size_t> previousIndices;
        std::vector<std::size_t> nextIndices;
    };

    // Both selections must be non-overlapping, face-balanced octrees with
    // transition masks matching every present neighbour. Unchanged chunks are
    // omitted. Every group can replace its previous chunks independently once
    // all of its next meshes are ready: no mixed-generation overlap, LOD gap or
    // incompatible transition face can cross between separate groups.
    //
    // Coverage may grow/shrink at the view boundary. A face with no neighbour
    // does not constrain its transition bit; initial coverage should still be
    // published together if exposing incomplete coverage is undesirable.
    // Groups follow the first next index (normally nearest-camera priority),
    // with removal-only groups last. Indices retain their input ordering.
    std::vector<LodChangeGroup> groupLodChanges(const std::vector<LodChunk>& previous,
                                               const std::vector<LodChunk>& next,
                                               std::uint32_t cells = 64);
}

#endif
