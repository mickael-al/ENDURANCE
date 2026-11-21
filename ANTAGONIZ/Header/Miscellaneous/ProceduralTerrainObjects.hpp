#ifndef ENDURANCE_PROCEDURAL_TERRAIN_OBJECTS_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_OBJECTS_HPP

#include "ProceduralTerrainBiomes.hpp"
#include "ProceduralTerrainFrustum.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ProceduralTerrain
{
    constexpr std::uint32_t ObjectEmptyType = 255;
    // Small pages limit wasted capacity when a sector contains only a few
    // instances of each of the many baked variants.
    constexpr std::uint32_t ObjectPageSize = 64;

    // Game-owned catalogue. A prototype is one drawable model, not a species.
    // IDs are array indices; append entries rather than reordering saved IDs.
    struct ObjectPrototype
    {
        std::string asset;
        std::uint32_t capacity = 65536;
        float boundingRadius = 1.0f; // Includes the allowed maximum scale/wind.
        float minimumScale = 0.8f, maximumScale = 5.0f;
    };
    struct ObjectGroup { std::uint32_t first = 0, count = 0; };
    struct ObjectBiomeProfile
    {
        std::uint32_t groups = 0xffffffffu; // Four uint8 group IDs, 255 = empty.
        std::uint32_t weights = 0;         // Four uint8 relative weights.
        float density = 0.0f;              // Acceptance probability inside a grove.
        float minimumNormalY = 0.82f;
        float forestPeriod = 1200.0f;
        float forestThreshold = 0.45f;
        float forestSoftness = 0.18f;
        float minimumAboveSea = 1.0f;
    };
    struct ObjectPlacementConfig
    {
        bool enabled = false;
        // Reuse existing terrain emission, up to LOD6. No extra terrain job.
        // Coverage uses compact octree intervals, independent of display LOD.
        std::uint32_t maximumLod = 2, maximumSectors = 512;
        float candidateSpacing = 16.0f, minimumDistance = 8.0f;
        float retentionDistance = 2048.0f; // Publication radius; resident terrain keeps its cached sites.
        std::vector<ObjectPrototype> prototypes;
        std::vector<ObjectGroup> groups;
        std::array<ObjectBiomeProfile, BiomeCount> biomes{};
    };

    // SSBO std430 uvec2: three sector-local UNORM16 coordinates and a site ID.
    // The type, world sector and seed are shared by an ObjectBlock. No matrix,
    // duplicated type or floating-point world-space hash is stored per object.
    struct PackedObjectPosition { std::uint32_t xy = 0, zSite = 0; };
    static_assert(sizeof(PackedObjectPosition) == 8, "Object positions use eight bytes");

    struct ObjectBlock
    {
        std::uint32_t prototype = 0, firstInstance = 0, instanceCount = 0;
        std::array<std::int32_t, 3> sectorLattice{};
        std::array<float, 3> origin{};
        float extent = 0.0f;
        std::uint32_t seed = 0;
        // Conservative cube includes model bounds, maximum scale and wind.
        std::array<float, 3> boundsOrigin{};
        float boundsExtent = 0.0f;
        // CPU identity of immutable page contents. Reusing a retired pool
        // range must invalidate consumers even when its bounds/count match.
        // Zero is reserved for callers without allocation tracking.
        std::uint64_t allocationRevision = 0;
        bool operator==(const ObjectBlock& b) const noexcept
        {
            return prototype==b.prototype && firstInstance==b.firstInstance && instanceCount==b.instanceCount &&
                sectorLattice==b.sectorLattice && origin==b.origin && extent==b.extent && seed==b.seed &&
                boundsOrigin==b.boundsOrigin && boundsExtent==b.boundsExtent && allocationRevision==b.allocationRevision;
        }
    };
    struct ObjectPlacementStats
    {
        std::uint32_t sectors = 0, blocks = 0, instances = 0;
        std::uint64_t poolBytes = 0, scratchBytes = 0, revision = 0;
        std::uint64_t captureJobs = 0, cachedJobs = 0, generatedInstances = 0;
        std::uint64_t capacityFailures = 0, sectorFailures = 0;
        std::uint64_t publicationChecks = 0, publicationRebuilds = 0, coarseCoverageLookups = 0;
    };

    // Integer anchor, also used by the GLSL candidate generator. Independent
    // of pool slots, atomic ordering, camera movement and terrain mesh LOD.
    inline std::uint32_t objectHash(std::uint32_t x) noexcept
    { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
    inline std::uint32_t objectSiteHash(std::int32_t x, std::int32_t z, std::uint32_t seed) noexcept
    { return objectHash(static_cast<std::uint32_t>(x) ^ objectHash(static_cast<std::uint32_t>(z) ^ seed)); }
    inline std::int32_t objectFloorMultiple(std::int32_t value, std::uint32_t span) noexcept
    {
        const std::int64_t v = value, s = span;
        return static_cast<std::int32_t>((v >= 0 ? v / s : -((-v + s - 1) / s)) * s);
    }
    inline void selectObjectBlocks(const std::vector<ObjectBlock>& blocks, const Frustum& frustum,
        std::vector<ObjectBlock>& visible)
    {
        visible.clear();
        for (const auto& block : blocks)
            if (intersectsFrustum(frustum, block.boundsOrigin, block.boundsExtent)) visible.push_back(block);
    }
}
#endif
