#ifndef ENDURANCE_PROCEDURAL_GENERATION_HPP
#define ENDURANCE_PROCEDURAL_GENERATION_HPP

#include "CustomRenderer.hpp"
#include "ProceduralTerrainField.hpp"
#include "ProceduralTerrainLod.hpp"
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
        std::uint32_t vertexBudgetMiB = 256;
        std::uint32_t maxResidentChunks = 2048;
        std::uint32_t maxSelectedChunks = 1024;
        ProceduralTerrain::FieldParameters field;
        float microDetailStrength = 0.15f;
        float microDetailScale = 2.0f;
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
        std::uint64_t cameraCullUpdates = 0;
        std::uint64_t shadowCullUpdates = 0;
        std::uint64_t drawListUploads = 0;
        std::uint64_t drawListUploadBytes = 0;
        std::uint32_t largestLodTransaction = 0;
        double lastPlanningMilliseconds = 0.0;
        double lastGenerationMilliseconds = 0.0;
        std::uint64_t transientFieldBytes = 0;
        bool rebuilding = false;
        bool resourceLimited = false;
    };

    // The scene is borrowed and must outlive this generator.
    ProceduralGeneration(SceneData& scene, std::uint32_t seed);
    ~ProceduralGeneration() override;
    ProceduralGeneration(const ProceduralGeneration&) = delete;
    ProceduralGeneration& operator=(const ProceduralGeneration&) = delete;

    // Lifecycle and drawing run on the thread owning the OpenGL context.
    bool initialize(const Config& config);
    void update(const glm::vec3& cameraWorldPosition);
    void shutdown() noexcept;
    void prepareRenderer() override;
    void drawRenderer() override;
    void drawShadowRenderer(unsigned int shadowIndex) override;
    bool isInitialized() const noexcept;
    const Stats& getStats() const noexcept;
    const std::string& getLastError() const noexcept;
    const Config& getConfig() const noexcept;
    // Published octree coverage, including chunks whose density was all empty.
    void getVisibleLodChunks(std::vector<ProceduralTerrain::LodChunk>& output) const;

    SceneData& getScene() noexcept { return m_scene; }
    const SceneData& getScene() const noexcept { return m_scene; }
    std::uint32_t getSeed() const noexcept { return m_seed; }

private:
    struct Impl;
    SceneData& m_scene;
    std::uint32_t m_seed;
    std::unique_ptr<Impl> m_impl;
    Config m_config;
    Stats m_stats;
    std::string m_lastError;
};

#endif // ENDURANCE_PROCEDURAL_GENERATION_HPP
