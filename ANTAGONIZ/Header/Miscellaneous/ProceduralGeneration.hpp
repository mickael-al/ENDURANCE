#ifndef ENDURANCE_PROCEDURAL_GENERATION_HPP
#define ENDURANCE_PROCEDURAL_GENERATION_HPP

#include "CustomRenderer.hpp"
#include "ProceduralTerrainField.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainLod.hpp"
#include "ProceduralTerrainMaterials.hpp"
#include "ProceduralTerrainFineSurface.hpp"
#include "ProceduralTerrainSea.hpp"
#include <glm/vec3.hpp>
#include <cstdint>
#include <memory>
#include <string>

struct SceneData;

class ProceduralGeneration final : public CustomRenderer
{
public:
    struct Config
    {
        float cellSize = 1.0f;
        std::uint32_t maxLod = 6;
        float viewDistance = 4096.0f;
        float lodDistanceFactor = 1.25f;
        std::uint32_t maxChunksPerFrame = 2;
        std::uint32_t maxInFlight = 2;
        std::uint32_t columnCacheEntries = 128;
        std::uint32_t densityCacheEntries = 32;
        bool measureGenerationTimings = true;
        std::uint32_t vertexBudgetMiB = 256;
        std::uint32_t maxResidentChunks = 2048;
        std::uint32_t maxSelectedChunks = 2048;
        ProceduralTerrain::FieldParameters field;
        ProceduralTerrain::ObjectPlacementConfig objects;
        float microDetailStrength = 0.15f;
        float microDetailScale = 2.0f;
        // Game-owned recipes; the renderer only owns their GPU representation.
        ProceduralTerrain::TerrainAppearance appearance;
        std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> grainImage;
        bool debugLod = false;
        bool collisionEnabled = true;
        float collisionRadius = 128.0f;
        std::string shaderDirectory = "../Asset/Shader/ProceduralTerrain";
    };

    struct Stats
    {
        std::uint32_t selectedChunks = 0;
        std::uint32_t visibleChunks = 0;
        std::uint32_t cameraDrawChunks = 0;
        std::uint32_t cameraCulledChunks = 0;
        std::uint32_t shadowDrawChunks = 0;
        std::uint32_t shadowCulledChunks = 0;
        std::uint32_t pendingLodGroups = 0;
        std::uint32_t queuedChunks = 0;
        std::uint32_t pendingJobs = 0;
        std::uint32_t residentChunks = 0;
        std::uint32_t collisionChunks = 0;
        std::uint32_t pendingCollisionJobs = 0;
        std::uint64_t failedCollisionJobs = 0;
        std::uint64_t residentVertices = 0;
        std::uint64_t allocatedBytes = 0;
        std::uint64_t budgetBytes = 0;
        std::uint64_t completedJobs = 0;
        std::uint64_t emptyJobs = 0;
        std::uint64_t failedJobs = 0;
        std::uint64_t lodSelections = 0;
        std::uint64_t publishedLodGroups = 0;
        std::uint64_t lookaheadPlansUsed = 0;
        std::uint64_t sameFrameHandoffs = 0;
        std::uint64_t stalePlansDiscarded = 0;
        std::uint64_t planningWaitFrames = 0;
        std::uint64_t cameraCullUpdates = 0;
        std::uint64_t shadowCullUpdates = 0;
        std::uint64_t drawListUploads = 0;
        std::uint64_t drawListUploadBytes = 0;
        std::uint64_t materialUploads = 0;
        std::uint64_t materialUploadBytes = 0;
        std::uint32_t largestLodTransaction = 0;
        double lastPlanningMilliseconds = 0.0;
        double lastGenerationMilliseconds = 0.0;
        std::uint64_t transientFieldBytes = 0;
        ProceduralTerrain::GpuStats generation;
        double coverageMilliseconds = 0.0;
        bool rebuilding = false;
        // Render coverage has reached the current camera selection and all
        // terrain planning/submissions have drained. Empty chunks count too.
        bool coverageComplete = false;
        // A stopped failed/capacity-limited transaction is incomplete, not idle
        // success. Collision capacity is reported separately by resourceLimited.
        bool generationBlocked = false;
        bool resourceLimited = false;
        // Outstanding CPU request/result, retained successor, and lack of an
        // active GPU transaction specifically because CPU planning is pending.
        bool cpuPlanning = false;
        bool lookaheadReady = false;
        bool plannerWaiting = false;
    };

    // The scene is borrowed and must outlive this generator.
    ProceduralGeneration(SceneData& scene, std::uint32_t seed);
    ~ProceduralGeneration() override;
    ProceduralGeneration(const ProceduralGeneration&) = delete;
    ProceduralGeneration& operator=(const ProceduralGeneration&) = delete;

    // Lifecycle and drawing run on the thread owning the OpenGL context.
    bool initialize(const Config& config);
    // Live appearance edits run on the GL thread and never regenerate terrain.
    // Invalid data is rejected without changing the last accepted appearance.
    bool setTerrainAppearance(const ProceduralTerrain::TerrainAppearance& appearance);
    bool setTerrainAppearance(const ProceduralTerrain::TerrainAppearance& appearance,
        float microDetailStrength, float microDetailScale);
    bool setTerrainDetail(float strength, float scale);
    // Changes only the render variant; never rebuilds terrain or shadow meshes.
    bool setTerrainDebugLod(bool enabled);
    void update(const glm::vec3& cameraWorldPosition);
    void shutdown() noexcept;
    void prepareRenderer() override;
    void drawRenderer() override;
    void drawShadowRenderer(unsigned int shadowIndex) override;
    std::uint64_t shadowCasterBoundsRevision() const override;
    std::uint64_t shadowContentRevision() const override;
    std::uint64_t shadowLayerContentRevision(unsigned layer, const glm::mat4& clip) const override;
    void appendShadowCasterBounds(std::vector<Ge::ShadowCasterBounds>& output) const override;
    bool isInitialized() const noexcept;
    const Stats& getStats() const noexcept;
    const std::string& getLastError() const noexcept;
    const Config& getConfig() const noexcept;
    // Published octree coverage, including chunks whose density was all empty.
    void getVisibleLodChunks(std::vector<ProceduralTerrain::LodChunk>& output) const;
    // Sea tiles follow the same published coverage transaction, including empty
    // terrain chunks. Borrowed until update()/shutdown(); no per-frame copy.
    const std::vector<ProceduralTerrain::SeaTile>& getSeaTiles() const noexcept;
    std::uint64_t getCoverageRevision() const noexcept;
    unsigned int getObjectInstanceBuffer() const noexcept;
    const std::vector<ProceduralTerrain::ObjectBlock>& getObjectBlocks() const noexcept;
    ProceduralTerrain::ObjectPlacementStats getObjectPlacementStats() const noexcept;
    const std::string& getObjectPlacementError() const noexcept;

    SceneData& getScene() noexcept { return m_scene; }
    const SceneData& getScene() const noexcept { return m_scene; }
    std::uint32_t getSeed() const noexcept { return m_seed; }

private:
    bool applyTerrainAppearance(const ProceduralTerrain::TerrainAppearance& appearance,
        float strength, float scale, bool debugLod);
    struct Impl;
    SceneData& m_scene;
    std::uint32_t m_seed;
    std::unique_ptr<Impl> m_impl;
    Config m_config;
    mutable Stats m_stats; // Per-layer visibility is a logical rendering cache.
    std::string m_lastError;
};

#endif // ENDURANCE_PROCEDURAL_GENERATION_HPP
