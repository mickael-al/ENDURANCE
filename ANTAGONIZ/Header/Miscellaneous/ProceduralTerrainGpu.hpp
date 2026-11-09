#ifndef ENDURANCE_PROCEDURAL_TERRAIN_GPU_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_GPU_HPP

#include "ProceduralTerrainField.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ProceduralTerrain
{
    struct GpuConfig
    {
        std::uint32_t cells = 64;
        float baseSpacing = 1.0f;
        std::uint32_t vertexBudgetMiB = 256;
        std::uint32_t maxResidentChunks = 2048;
        std::uint32_t maxInFlight = 2;
        // Bounded GPU caches, including slots pinned by jobs. Zero retains no
        // completed entry; in-flight jobs still share identical fields safely.
        std::uint32_t columnCacheEntries = 128;
        std::uint32_t densityCacheEntries = 32;
        bool measureGenerationTimings = true;
        std::string shaderDirectory = "../Asset/Shader/ProceduralTerrain";
        FieldParameters field;
    };

    struct ChunkRequest
    {
        // Integer coordinates on the finest voxel lattice. Must be aligned to
        // cells * (1 << lod). Face bits: -X,+X,-Y,+Y,-Z,+Z.
        std::array<std::int32_t, 3> origin{};
        std::uint32_t lod = 0;
        std::uint32_t transitionMask = 0;
        std::uint32_t seed = 0;
    };

    struct MeshHandle
    {
        std::uint32_t slot = UINT32_MAX;
        std::uint32_t generation = 0;
        std::uint32_t firstVertex = 0;
        std::uint32_t vertexCount = 0;
        std::array<float, 3> origin{};
        // Finest chunk-lattice coordinates, shared by rendering and collision
        // for one canonical FMA decode across all neighboring LOD levels.
        std::array<std::int32_t, 3> baseChunkCoordinate{};
        float extent = 0.0f;
        std::uint32_t lod = 0;
        std::uint32_t transitionMask = 0;
    };

    enum class CompletionStatus { Ready, Empty, Failed, Cancelled };

    struct Completion
    {
        std::uint64_t token = 0;
        CompletionStatus status = CompletionStatus::Failed;
        MeshHandle mesh;
        std::string error;
    };

    struct GpuStats
    {
        std::uint32_t pendingJobs = 0;
        std::uint32_t residentChunks = 0;
        std::uint64_t residentVertices = 0;
        std::uint64_t allocatedBytes = 0;
        std::uint64_t budgetBytes = 0;
        std::uint64_t completedJobs = 0;
        std::uint64_t emptyJobs = 0;
        std::uint64_t failedJobs = 0;
        // CPU-to-GPU command uploads, not vertex/field transfers.
        std::uint64_t drawListUploads = 0;
        std::uint64_t drawListUploadBytes = 0;
        double lastGenerationMilliseconds = 0.0;
        std::uint64_t transientFieldBytes = 0;
        std::uint64_t biomeMetadataBytes = 0;
        std::uint64_t fieldBatches = 0;
        std::uint32_t lastFieldBatchJobs = 0;
        std::uint64_t columnCacheHits = 0, densityCacheHits = 0;
        std::uint64_t generatedColumnTiles = 0, generatedDensityFields = 0;
        std::uint64_t cachedFieldBytes = 0, groupMetadataBytes = 0;
        std::uint64_t submittedBiomePasses = 0;
        std::uint64_t timedBatches = 0, timedEmissions = 0, droppedTimingSamples = 0;
        std::uint32_t pendingTimingSamples = 0;
        // Disjoint GPU intervals: shared field/count work is counted once per
        // batch, emission once per job. Query results are collected later.
        double field2DMilliseconds = 0.0, fieldPreparationMilliseconds = 0.0;
        double volumeMilliseconds = 0.0, countMilliseconds = 0.0, emissionMilliseconds = 0.0;
        double lastField2DMilliseconds = 0.0, lastFieldPreparationMilliseconds = 0.0;
        double lastVolumeMilliseconds = 0.0, lastCountMilliseconds = 0.0, lastEmissionMilliseconds = 0.0;
        double lastRequestMilliseconds = 0.0, totalRequestMilliseconds = 0.0;
        std::uint64_t largestFreeVertexBytes = 0;
        std::uint32_t freeVertexRanges = 0;
    };

    // All calls, including destruction, belong to the owning OpenGL context.
    // Generation retains density/geometry on the GPU. poll() only examines
    // completed fences and a tiny, persistently mapped counter block.
    class GpuMesher
    {
    public:
        GpuMesher();
        ~GpuMesher();
        GpuMesher(const GpuMesher&) = delete;
        GpuMesher& operator=(const GpuMesher&) = delete;

        bool initialize(const GpuConfig& config, std::string& error);
        void shutdown() noexcept;
        // Queue requests until poll(), allowing all ready chunks to share each
        // biome's indirect dispatch. Capacity includes queued requests.
        bool submit(const ChunkRequest& request, std::uint64_t token);
        // Submit the current batch without waiting or consuming completions.
        // Frame schedulers call this after enqueuing all ready chunks; poll()
        // also submits any remaining queue for standalone callers.
        void dispatchQueued();
        // Idle-only benchmark/debug invalidation. Keeps programs, arenas and
        // published meshes; refuses to invalidate slots referenced by jobs.
        bool clearFieldCaches();
        void poll(std::vector<Completion>& completions);
        void cancel(std::uint64_t token) noexcept;
        void release(const MeshHandle& mesh) noexcept;
        bool valid(const MeshHandle& mesh) const noexcept;

        // The caller binds its render/shadow program. These methods bind only
        // geometry, indirect commands and the per-chunk descriptor at SSBO 8.
        void draw(const MeshHandle& mesh) const;
        // Keep camera/cascade command lists independent. Use a stable ID per
        // render pass (for example camera=0, shadow layer=index+1).
        void drawMany(const std::vector<MeshHandle>& meshes, std::uint32_t drawList = 0);
        unsigned int vertexBuffer() const noexcept;
        unsigned int descriptorBuffer() const noexcept;
        unsigned int vertexArray() const noexcept;
        const GpuConfig& config() const noexcept;
        GpuStats stats() const noexcept;
        const std::string& lastError() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

#endif
