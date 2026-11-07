#ifndef ENDURANCE_PROCEDURAL_TERRAIN_GPU_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_GPU_HPP

#include "ProceduralTerrainField.hpp"
#include "BackgroundShaderCompiler.hpp"
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
        // Bounded cache pinned until shutdown. Generic programs remain available
        // while variants compile, when full, or without a shared compiler context.
        bool enableBiomeVariants = true;
        std::uint32_t maxBiomeVariants = 32;
        std::uint32_t maxParallelBiomeCompiles = 2;
        // Optional shared-context provider. Without one, runtime compilation
        // stays disabled rather than falling back to the rendering thread.
        Ge::BackgroundShaderCompiler::ContextHooks shaderCompilerContext;
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
        double lastClassificationMilliseconds = 0.0;
        std::uint64_t transientFieldBytes = 0;
        std::uint64_t biomeMetadataBytes = 0;
        std::uint64_t biomeVariantHits = 0, biomeVariantFallbacks = 0;
        std::uint64_t biomeVariantCompiles = 0, biomeVariantCacheFull = 0;
        std::uint64_t biomeCompileFailures = 0;
        std::uint32_t biomeVariants = 0, pendingBiomeVariants = 0;
        bool parallelShaderCompilation = false;
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
        bool submit(const ChunkRequest& request, std::uint64_t token);
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
        unsigned int indirectBuffer() const noexcept;
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
