#ifndef ENDURANCE_PROCEDURAL_TERRAIN_COLLISION_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_COLLISION_HPP

#include "ProceduralTerrainGpu.hpp"
#include <glm/vec3.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ge { class PhysicsWraper; }

namespace ProceduralTerrain
{
    struct CollisionConfig
    {
        float radius = 128.0f;
        std::uint32_t maxResidentChunks = 128;
        std::uint32_t maxPendingReadbacks = 2;
        std::uint32_t maxCopiesPerFrame = 1;
        // Per staging slot. CPU workers decode the persistent mapping directly;
        // there is no additional packed-vertex copy on the rendering thread.
        std::uint32_t maxReadbackMiB = 16;
        std::uint32_t maxPendingBuilds = 4;
        std::uint32_t workerCount = 2;
        std::uint32_t maxCopyMiBPerFrame = 16;
    };

    struct CollisionStats
    {
        std::uint32_t residentChunks = 0;
        std::uint32_t pendingReadbacks = 0;
        std::uint32_t pendingBuilds = 0;
        std::uint64_t copiedBytes = 0;
        std::uint64_t stagingBytes = 0;
        std::uint64_t completedChunks = 0;
        std::uint64_t failedChunks = 0;
        bool capacityLimited = false;
    };

    // Local LOD-0 collision cache, independent of visual mesh residency.
    // update/shutdown/destruction require the owning GL context and a live
    // physics command queue. The physics wrapper must outlive this cache.
    // The caller must request LOD 0 throughout the desired collision radius.
    class CollisionCache
    {
    public:
        CollisionCache();
        ~CollisionCache();
        CollisionCache(const CollisionCache&) = delete;
        CollisionCache& operator=(const CollisionCache&) = delete;

        bool initialize(Ge::PhysicsWraper* physics, const CollisionConfig& config);
        void update(const glm::vec3& position, const std::vector<MeshHandle>& meshes,
                    const GpuMesher& mesher);
        void shutdown() noexcept;
        CollisionStats stats() const noexcept;
        // A failed mesh is not retried until its handle changes. Existing
        // colliders remain usable; diagnostics identify the rejected work.
        const std::string& lastError() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

#endif
