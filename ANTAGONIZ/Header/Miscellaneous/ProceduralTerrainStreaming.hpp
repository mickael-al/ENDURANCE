#ifndef ENDURANCE_PROCEDURAL_TERRAIN_STREAMING_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_STREAMING_HPP

#include "ProceduralTerrainLod.hpp"
#include <cstddef>
#include <vector>

namespace ProceduralTerrain
{
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
