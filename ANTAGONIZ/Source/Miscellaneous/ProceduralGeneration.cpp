#include "glcore.hpp"
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainLod.hpp"
#include "ProceduralTerrainFrustum.hpp"
#include "ProceduralTerrainStreaming.hpp"
#include "ProceduralTerrainPlanner.hpp"
#include "ProceduralTerrainVisibility.hpp"
#include "ProceduralTerrainShadowVisibility.hpp"
#include "ProceduralTerrainCollision.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "Camera.hpp"
#include "GraphiquePipeline.hpp"
#include "ShaderPair.hpp"
#include <glm/mat4x4.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using namespace ProceduralTerrain;
    struct ChunkKey
    {
        std::array<std::int32_t, 3> origin{};
        std::uint32_t lod = 0, mask = 0;
        bool operator==(const ChunkKey& other) const noexcept
        { return origin == other.origin && lod == other.lod && mask == other.mask; }
    };
    struct KeyHash
    {
        std::size_t operator()(const ChunkKey& k) const noexcept
        {
            std::size_t h = 2166136261u;
            for (auto v : k.origin) h = (h ^ static_cast<std::uint32_t>(v)) * 16777619u;
            h = (h ^ k.lod) * 16777619u;
            return (h ^ k.mask) * 16777619u;
        }
    };
    using KeySet = std::unordered_set<ChunkKey, KeyHash>;

    // Only the terrain material pass touches this binding. In particular, the
    // atmosphere uses UBO 6 too, so restore both an existing range and the
    // generic binding after a custom terrain draw.
    struct TerrainMaterialStateGuard
    {
        GLint uniform = 0, material = 0;
        GLint64 start = 0, size = 0;
        TerrainMaterialStateGuard()
        {
            glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &uniform);
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 6, &material);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 6, &start);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, 6, &size);
        }
        ~TerrainMaterialStateGuard()
        {
            if (material && size > 0)
                glBindBufferRange(GL_UNIFORM_BUFFER, 6, material, start, size);
            else glBindBufferBase(GL_UNIFORM_BUFFER, 6, material);
            glBindBuffer(GL_UNIFORM_BUFFER, uniform);
        }
    };

    // Custom renderers must leave the enclosing material/shadow pass intact.
    struct TerrainRenderStateGuard
    {
        GLint program = 0, vao = 0, indirect = 0, storage = 0, descriptor = 0;
        GLint64 descriptorStart = 0, descriptorSize = 0;
        TerrainRenderStateGuard()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &indirect);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &storage);
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 8, &descriptor);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, 8, &descriptorStart);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, 8, &descriptorSize);
        }
        ~TerrainRenderStateGuard()
        {
            glUseProgram(program);
            glBindVertexArray(vao);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect);
            if (descriptor && descriptorSize > 0)
                glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 8, descriptor, descriptorStart, descriptorSize);
            else glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, descriptor);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, storage);
        }
    };

    struct ComputeState
    {
        GLint program = 0, storage = 0, dispatchIndirect = 0;
        // Only these bindings are touched by the field/meshing passes.
        static constexpr std::array<GLuint, 8> indices{{0, 1, 2, 3, 4, 5, 6, 7}};
        std::array<GLint, 8> bindings{};
        std::array<GLint64, 8> starts{}, sizes{};
        ComputeState()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &storage);
            glGetIntegerv(GL_DISPATCH_INDIRECT_BUFFER_BINDING, &dispatchIndirect);
            for (GLuint i = 0; i < bindings.size(); ++i)
            {
                glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, indices[i], &bindings[i]);
                glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, indices[i], &starts[i]);
                glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, indices[i], &sizes[i]);
            }
        }
        ~ComputeState()
        {
            glUseProgram(program);
            for (GLuint i = 0; i < bindings.size(); ++i)
            {
                if (bindings[i] && sizes[i] > 0)
                    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, indices[i], bindings[i], starts[i], sizes[i]);
                else glBindBufferBase(GL_SHADER_STORAGE_BUFFER, indices[i], bindings[i]);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, storage);
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, dispatchIndirect);
        }
    };


}

struct ProceduralGeneration::Impl
{
    const ptrClass* engine = nullptr;
    Ge::GraphiquePipelineManager* pipelines = nullptr;
    Ge::GraphiquePipeline* render = nullptr;
    Ge::GraphiquePipeline* shadow = nullptr;
    ProceduralGeneration* registered = nullptr;
    GpuMesher gpu;
    CollisionCache collision;
    LodConfig lod;
    struct ReadyChunk
    {
        MeshHandle mesh;
        bool hasMarineSurface = false;
    };
    std::unordered_map<ChunkKey, ReadyChunk, KeyHash> ready;
    std::unordered_map<std::uint64_t, ChunkKey> pending;
    KeySet submitted, wanted, visibleKeys, collisionKeys;
    std::unique_ptr<LodPlanner> planner;
    LodTransaction transaction;
    struct PreparedPlan
    {
        LodPlanResult result;
        bool lookahead = false;
    };
    std::optional<PreparedPlan> preparedPlan;
    bool plannerLookahead = false, discardPlannerResult = false;
    std::vector<LodChunk> visibleSelection;
    std::vector<SeaTile> seaTiles;
    std::vector<Ge::ShadowCasterBounds> shadowCasterBounds;
    std::uint64_t shadowBoundsRevision = Ge::nextShadowCasterBoundsRevision();
    CameraVisibilityCache cameraVisibility;
    using ShadowVisibility = ShadowMeshVisibilityCache;
    std::unordered_map<unsigned int, ShadowVisibility> shadowVisibility;
    struct CameraRange
    {
        Ge::Camera* camera;
        float originalFar, appliedFar;
    };
    std::vector<CameraRange> cameraRanges;
    std::vector<ChunkKey> target;
    std::vector<MeshHandle> visible, cameraMeshes, collisionCandidates;
    std::vector<Completion> completions;
    glm::vec3 plannedPosition{};
    glm::vec3 collisionPosition{};
    std::uint64_t nextToken = 1, visibleRevision = 0;
    std::chrono::steady_clock::time_point coverageStart;
    bool timingCoverage = false;
    std::size_t desiredCount = 0;
    bool planned = false, active = false, converged = false, blocked = false;
    bool collisionDirty = true, collisionUpdated = false;
    GLint shadowIndex = -1;
    GLuint materialBuffer = 0;
    struct MaterialUniforms
    {
        GLint macroPeriod = -1, patchPeriod = -1;
        GLint detailStrength = -1, detailScale = -1;
        GLint albedoGamma = -1;
    } materialUniforms;
    float materialAlbedoGamma = -1.0f;
    struct RenderVariant
    {
        unsigned key = 0;
        std::uint64_t lastUse = 0;
        Ge::GraphiquePipeline* pipeline = nullptr;
        MaterialUniforms uniforms;
    };
    // Editor diagnostics are lazy, bounded and keyed only by switches. Numeric
    // palette/noise controls remain uniforms; no per-biome production explosion.
    static constexpr std::size_t MaximumRenderVariants = 8;
    std::vector<RenderVariant> renderVariants;
    std::uint64_t renderUse = 0;

    void destroyPipeline(Ge::GraphiquePipeline*& pipeline)
    {
        if (!pipeline) return;
        auto* pair = pipeline->getShaderPair();
        pipelines->destroyPipeline(pipeline);
        delete pair;
        pipeline = nullptr;
    }

    bool selectRenderVariant(const Config& config, std::uint32_t seed, std::string& error)
    {
        const unsigned debug = config.debugLod ? 0u : static_cast<unsigned>(config.appearance.debugView);
        const unsigned key = debug | (config.debugLod ? 8u : 0u)
            | (config.microDetailStrength > 0.0f ? 16u : 0u);
        auto found = std::find_if(renderVariants.begin(), renderVariants.end(),
            [key](const RenderVariant& variant) { return variant.key == key; });
        if (found != renderVariants.end() && found->pipeline == render) return true;
        if (found == renderVariants.end())
        {
            std::string defines = "#define DEBUG_TERRAIN_MATERIAL " + std::to_string(debug)
                + "\n#define TERRAIN_FIXED_BIOME " + std::to_string(config.field.biomeOverride)
                + "\n#define TERRAIN_MICRODETAIL_ENABLED " + (config.microDetailStrength > 0.0f ? "1\n" : "0\n");
            if (config.debugLod) defines += "#define DEBUG_TERRAIN_LOD 1\n";
            Ge::ShaderCacheOptions cache;
            cache.enabled = true; // Includes expanded source + defines in binary key.
            RenderVariant candidate;
            candidate.key = key;
            candidate.pipeline = pipelines->createPipeline(config.shaderDirectory + "/render.fs.glsl",
                config.shaderDirectory + "/render.vs.glsl", false, true, false, 1, defines, cache);
            if (!candidate.pipeline || !candidate.pipeline->isValid())
            {
                error = candidate.pipeline ? candidate.pipeline->getLastError()
                    : "Terrain material variant was not created.";
                destroyPipeline(candidate.pipeline);
                return false;
            }
            const GLuint program = candidate.pipeline->getProgram();
            const GLuint block = glGetUniformBlockIndex(program, "TerrainMaterialTable");
            GLint bytes = 0;
            if (block != GL_INVALID_INDEX)
                glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &bytes);
            if (bytes != static_cast<GLint>(sizeof(TerrainMaterialTable)))
            {
                error = "Terrain material shader/table layout mismatch; reload the terrain shaders.";
                destroyPipeline(candidate.pipeline);
                return false;
            }
            auto& u = candidate.uniforms;
            u.macroPeriod = glGetUniformLocation(program, "terrainMacroPeriod");
            u.patchPeriod = glGetUniformLocation(program, "terrainPatchPeriod");
            u.detailStrength = glGetUniformLocation(program, "microDetailStrength");
            u.detailScale = glGetUniformLocation(program, "microDetailScale");
            u.albedoGamma = glGetUniformLocation(program, "terrainAlbedoGamma");
            glProgramUniform1uiv(program, glGetUniformLocation(program, "terrainBiomeMatrix[0]"),
                static_cast<GLsizei>(BiomeMatrix2D.size()), BiomeMatrix2D.data());
            glProgramUniform1ui(program, glGetUniformLocation(program, "worldSeed"), seed);
            glProgramUniform1f(program, glGetUniformLocation(program, "terrainSeaLevel"), config.field.seaLevel);
            glProgramUniform1f(program, glGetUniformLocation(program, "terrainSeaBlendWidth"), config.field.seaBlendWidth);
            // Keep the previous usable program until the new one linked and its
            // ABI was checked. Never evict the active variant on a failed build.
            if (renderVariants.size() == MaximumRenderVariants)
            {
                auto oldest = std::min_element(renderVariants.begin(), renderVariants.end(),
                    [](const RenderVariant& a, const RenderVariant& b) { return a.lastUse < b.lastUse; });
                destroyPipeline(oldest->pipeline);
                *oldest = candidate;
                found = oldest;
            }
            else
            {
                renderVariants.push_back(candidate);
                found = renderVariants.end() - 1;
            }
        }
        found->lastUse = ++renderUse;
        render = found->pipeline;
        materialUniforms = found->uniforms;
        const GLuint program = render->getProgram();
        // Re-activated variants may predate many live palette/control edits.
        // Restore all controls, not only the field changed by this UI action.
        glProgramUniform1f(program, materialUniforms.macroPeriod, config.appearance.macroPeriod);
        glProgramUniform1f(program, materialUniforms.patchPeriod, config.appearance.patchPeriod);
        glProgramUniform1f(program, materialUniforms.detailStrength, config.microDetailStrength);
        glProgramUniform1f(program, materialUniforms.detailScale, config.microDetailScale);
        materialAlbedoGamma = -1.0f;
        refreshAlbedoGamma();
        return true;
    }

    ShadowVisibility& selectShadow(unsigned index, const glm::mat4& clip, Stats& stats)
    {
        auto& selected = shadowVisibility[index];
        if (selected.update(clip, visibleRevision, visible)) ++stats.shadowCullUpdates;
        return selected;
    }

    void refreshAlbedoGamma()
    {
        float gamma = engine->settingManager ? engine->settingManager->getGamma() : 2.2f;
        if (!finiteFieldValue(gamma) || gamma <= 0.0f) gamma = 2.2f;
        gamma = (std::max)(gamma, 0.001f);
        if (gamma == materialAlbedoGamma) return;
        // Existing deferred lighting decodes gColor with pow(color, gamma).
        // Encode our linear artist palettes exactly once to preserve that
        // shared G-buffer contract, including live engine gamma changes.
        glProgramUniform1f(render->getProgram(), materialUniforms.albedoGamma, gamma);
        materialAlbedoGamma = gamma;
    }

    ~Impl()
    {
        if (registered && pipelines) pipelines->removeCustomRenderer(registered);
        planner.reset();
        collision.shutdown();
        gpu.shutdown();
        if (materialBuffer) glDeleteBuffers(1, &materialBuffer);
        const auto liveObjects = Ge::GObject::GetGObjects();
        for (const auto& range : cameraRanges)
        {
            const auto found = std::find(liveObjects.begin(), liveObjects.end(), range.camera);
            if (found == liveObjects.end()) continue;
            auto* camera = dynamic_cast<Ge::Camera*>(*found);
            if (camera && camera->getFar() == range.appliedFar) camera->setFar(range.originalFar);
        }
        // These pairs were created exclusively for this generator. The engine
        // manager destroys the GL pipeline but deliberately retains its pair.
        destroyPipeline(shadow);
        for (auto& variant : renderVariants) destroyPipeline(variant.pipeline);
        render = nullptr;
    }

    void discardUnused()
    {
        for (auto it = ready.begin(); it != ready.end();)
        {
            if (visibleKeys.count(it->first) || wanted.count(it->first)) { ++it; continue; }
            gpu.release(it->second.mesh);
            it = ready.erase(it);
        }
    }

    void abandonTarget()
    {
        for (const auto& job : pending) gpu.cancel(job.first);
        pending.clear();
        submitted.clear();
        wanted.clear();
        target.clear();
        transaction = {};
        active = false;
        preparedPlan.reset();
        // A failed transaction never publishes its predicted revision. An old
        // successor must not become valid if a later transaction reaches the
        // same revision number after recovery.
        discardPlannerResult = planner && planner->busy();
        collisionDirty = true;
        discardUnused();
        gpu.discardUnpublishedObjects();
    }
};

ProceduralGeneration::ProceduralGeneration(SceneData& scene, std::uint32_t seed)
    : m_scene(scene), m_seed(seed) {}
ProceduralGeneration::~ProceduralGeneration() { shutdown(); }

bool ProceduralGeneration::initialize(const Config& config)
{
    shutdown();
    m_lastError.clear();
    const auto finite = [](float x) { return ProceduralTerrain::finiteFieldValue(x); };
    if (!finite(config.cellSize) || config.cellSize < 0.001f || config.cellSize > 1024.0f
        || config.maxLod > 12 || !finite(config.viewDistance) || config.viewDistance <= 0.0f
        || config.viewDistance > 1000000.0f || !finite(config.lodDistanceFactor)
        || config.lodDistanceFactor <= 0.0f || config.lodDistanceFactor > 16.0f
        || config.maxChunksPerFrame == 0 || config.maxChunksPerFrame > 64
        || config.maxInFlight == 0 || config.maxInFlight > 16
        || config.columnCacheEntries > 4096 || config.densityCacheEntries > 4096
        || config.maxSelectedChunks == 0 || config.maxSelectedChunks > 32768
        || config.maxResidentChunks < config.maxSelectedChunks
        || !finite(config.collisionRadius) || config.collisionRadius < 0.0f
        || (config.collisionEnabled && config.collisionRadius == 0.0f)
        || config.collisionRadius > config.viewDistance
        || !finite(config.microDetailStrength) || config.microDetailStrength < 0.0f || config.microDetailStrength > 16.0f
        || !finite(config.microDetailScale) || config.microDetailScale <= 0.0f || config.microDetailScale > 1000000.0f
        || !validTerrainAppearance(config.appearance)
        || !validFieldParameters(config.field))
    {
        m_lastError = "Invalid terrain configuration (finite values, positive sizes and bounded budgets required).";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->engine = Ge::Engine::getPtrClassAddr();
    if (!impl->engine || !impl->engine->graphiquePipelineManager)
    {
        m_lastError = "Terrain requires the running rendering engine.";
        return false;
    }
    impl->pipelines = impl->engine->graphiquePipelineManager;
    ComputeState computeState;
    GpuConfig gpuConfig;
    gpuConfig.baseSpacing = config.cellSize;
    gpuConfig.vertexBudgetMiB = config.vertexBudgetMiB;
    gpuConfig.maxResidentChunks = config.maxResidentChunks;
    gpuConfig.maxInFlight = config.maxInFlight;
    gpuConfig.columnCacheEntries = config.columnCacheEntries;
    gpuConfig.densityCacheEntries = config.densityCacheEntries;
    gpuConfig.measureGenerationTimings = config.measureGenerationTimings;
    gpuConfig.shaderDirectory = config.shaderDirectory;
    gpuConfig.field = config.field;
    gpuConfig.objects = config.objects;
    if (!impl->gpu.initialize(gpuConfig, m_lastError)) return false;

    const auto path = [&config](const char* name) { return config.shaderDirectory + "/" + name; };
    Ge::ShaderCacheOptions shaderCache;
    shaderCache.enabled = true;
    if (!impl->selectRenderVariant(config, m_seed, m_lastError)) return false;
    impl->shadow = impl->pipelines->createPipeline(path("shadow.fs.glsl"), path("shadow.vs.glsl"),
        false, true, false, 1, "", shaderCache);
    if (!impl->shadow || !impl->shadow->isValid())
    {
        m_lastError = impl->shadow ? impl->shadow->getLastError()
            : "Terrain shadow pipeline was not created.";
        return false;
    }
    glCreateBuffers(1, &impl->materialBuffer);
    glNamedBufferData(impl->materialBuffer, sizeof(TerrainMaterialTable),
        config.appearance.biomes.data(), GL_DYNAMIC_DRAW);
    GLint64 allocatedMaterialBytes = 0;
    glGetNamedBufferParameteri64v(impl->materialBuffer, GL_BUFFER_SIZE, &allocatedMaterialBytes);
    if (allocatedMaterialBytes != static_cast<GLint64>(sizeof(TerrainMaterialTable)))
    {
        m_lastError = "Terrain material GPU table allocation failed.";
        return false;
    }
    impl->shadowIndex = glGetUniformLocation(impl->shadow->getProgram(), "offsetShadow");

    impl->lod.baseSpacing = config.cellSize;
    impl->lod.maxLod = config.maxLod;
    impl->lod.viewDistance = config.viewDistance;
    impl->lod.splitDistanceFactor = config.lodDistanceFactor;
    impl->lod.maxLeaves = config.maxSelectedChunks;
    impl->lod.fullResolutionRadius = config.collisionEnabled ? config.collisionRadius : 0.0f;
    auto bounds = fieldHeightBounds(config.field);
    if (config.field.mode == FieldMode::Terrain)
    {
        // Empty sea-surface leaves still own water. Keep them in the planner
        // when seaLevel lies above/below the terrain's possible density band.
        bounds.minimumY = (std::min)(bounds.minimumY, config.field.seaLevel);
        bounds.maximumY = (std::max)(bounds.maximumY, config.field.seaLevel);
    }
    const float padding = std::ldexp(config.cellSize, static_cast<int>(config.maxLod));
    impl->lod.minimumY = bounds.minimumY - padding;
    impl->lod.maximumY = bounds.maximumY + padding;
    impl->planner = std::make_unique<LodPlanner>(impl->lod);
    if (config.collisionEnabled)
    {
        CollisionConfig collisionConfig;
        collisionConfig.radius = config.collisionRadius;
        if (!impl->collision.initialize(impl->engine->physicsEngine, collisionConfig))
        {
            m_lastError = "Terrain collision cache could not initialize.";
            return false;
        }
    }
    m_config = config;
    m_stats = {};
    m_stats.materialUploads = 1;
    m_stats.materialUploadBytes = sizeof(TerrainMaterialTable);
    impl->pipelines->addCustomRenderer(this);
    impl->registered = this;
    m_impl = std::move(impl);
    return true;
}

bool ProceduralGeneration::setTerrainAppearance(const TerrainAppearance& appearance)
{
    return applyTerrainAppearance(appearance, m_config.microDetailStrength, m_config.microDetailScale, m_config.debugLod);
}

bool ProceduralGeneration::setTerrainDetail(float strength, float scale)
{
    return applyTerrainAppearance(m_config.appearance, strength, scale, m_config.debugLod);
}

bool ProceduralGeneration::setTerrainAppearance(const TerrainAppearance& appearance, float strength, float scale)
{
    return applyTerrainAppearance(appearance, strength, scale, m_config.debugLod);
}

bool ProceduralGeneration::setTerrainDebugLod(bool enabled)
{
    return applyTerrainAppearance(m_config.appearance, m_config.microDetailStrength, m_config.microDetailScale, enabled);
}

bool ProceduralGeneration::applyTerrainAppearance(const TerrainAppearance& appearance,
    float strength, float scale, bool debugLod)
{
    // Validation and a potentially new program must succeed before any table or
    // numeric control changes. Failed compilation leaves the displayed state.
    if (!m_impl || !validTerrainAppearance(appearance)
        || !finiteFieldValue(strength) || strength < 0.0f || strength > 16.0f
        || !finiteFieldValue(scale) || scale <= 0.0f || scale > 1000000.0f)
    {
        m_lastError = "Invalid terrain appearance or renderer not initialized; retaining all previous material settings.";
        return false;
    }
    auto& s = *m_impl;
    Config next = m_config;
    next.appearance = appearance;
    next.microDetailStrength = strength;
    next.microDetailScale = scale;
    next.debugLod = debugLod;
    auto* previousProgram = s.render;
    std::string variantError;
    if (!s.selectRenderVariant(next, m_seed, variantError))
    {
        m_lastError = "Terrain render variant: " + variantError;
        return false;
    }
    const auto& previous = m_config.appearance;
    if (std::memcmp(previous.biomes.data(), appearance.biomes.data(), sizeof(TerrainMaterialTable)) != 0)
    {
        glNamedBufferData(s.materialBuffer, sizeof(TerrainMaterialTable), appearance.biomes.data(), GL_DYNAMIC_DRAW);
        ++m_stats.materialUploads;
        m_stats.materialUploadBytes += sizeof(TerrainMaterialTable);
    }
    // A newly selected variant already received every numeric control.
    if (s.render == previousProgram)
    {
        const GLuint program = s.render->getProgram();
        if (previous.macroPeriod != appearance.macroPeriod)
            glProgramUniform1f(program, s.materialUniforms.macroPeriod, appearance.macroPeriod);
        if (previous.patchPeriod != appearance.patchPeriod)
            glProgramUniform1f(program, s.materialUniforms.patchPeriod, appearance.patchPeriod);
        if (m_config.microDetailStrength != strength)
            glProgramUniform1f(program, s.materialUniforms.detailStrength, strength);
        if (m_config.microDetailScale != scale)
            glProgramUniform1f(program, s.materialUniforms.detailScale, scale);
    }
    m_config = std::move(next);
    // A successful appearance edit must not hide a terrain-generation failure.
    if (m_lastError.rfind("Terrain render variant: ", 0) == 0
        || m_lastError == "Invalid terrain appearance or renderer not initialized; retaining all previous material settings.")
        m_lastError.clear();
    return true;
}

void ProceduralGeneration::shutdown() noexcept
{
    m_impl.reset();
    m_stats = {};
}

void ProceduralGeneration::prepareRenderer()
{
    if (!m_impl) return;
    auto& s = *m_impl;
    m_stats.cameraDrawChunks = m_stats.cameraCulledChunks = 0;
    m_stats.shadowDrawChunks = m_stats.shadowCulledChunks = 0;
    auto* camera = s.engine->cameraManager ? s.engine->cameraManager->getCurrentCamera() : nullptr;
    if (!camera) return;
    const auto saved = std::find_if(s.cameraRanges.begin(), s.cameraRanges.end(),
        [camera](const Impl::CameraRange& range) { return range.camera == camera; });
    const float requiredFar = m_config.viewDistance
        + std::ldexp(64.0f * m_config.cellSize, static_cast<int>(m_config.maxLod));
    if (saved == s.cameraRanges.end() && camera->getFar() < requiredFar)
    {
        ComputeState bindings;
        s.cameraRanges.push_back({camera, camera->getFar(), requiredFar});
        camera->setFar(requiredFar);
    }
    update(camera->getPosition());
}

void ProceduralGeneration::update(const glm::vec3& cameraPosition)
{
    if (!m_impl) return;
    auto& s = *m_impl;
    if (!finiteFieldValue(cameraPosition.x) || !finiteFieldValue(cameraPosition.y)
        || !finiteFieldValue(cameraPosition.z))
    {
        m_lastError = "Terrain camera position must be finite.";
        return;
    }
    // Idle terrain needs neither binding queries nor a full selection walk.
    std::optional<ComputeState> computeState;
    if (s.gpu.stats().pendingJobs) computeState.emplace();
    bool failed = false;
    const auto collectCompletions = [&]
    {
        s.completions.clear();
        s.gpu.poll(s.completions); // Zero-timeout fence checks, never wait for work.
        for (const auto& result : s.completions)
        {
            auto pending = s.pending.find(result.token);
            if (pending == s.pending.end()) { s.gpu.release(result.mesh); continue; }
            const ChunkKey key = pending->second;
            s.pending.erase(pending);
            s.submitted.erase(key);
            if (!s.wanted.count(key)) { s.gpu.release(result.mesh); continue; }
            if (result.status == CompletionStatus::Ready || result.status == CompletionStatus::Empty)
            {
                s.ready.emplace(key, Impl::ReadyChunk{result.mesh, result.hasMarineSurface});
                s.collisionDirty = true;
            }
            else if (result.status == CompletionStatus::Failed)
            {
                m_lastError = result.error.empty() ? "GPU terrain generation failed." : result.error;
                failed = true;
            }
        }
    };
    collectCompletions();
    s.gpu.trimObjectSectors(cameraPosition);

    const auto distanceSquared = [](const glm::vec3& a, const glm::vec3& b)
    {
        const auto delta = glm::dvec3(a) - glm::dvec3(b);
        return glm::dot(delta, delta);
    };
    const double replanDistance = 32.0 * m_config.cellSize;
    const bool moved = !s.planned || distanceSquared(cameraPosition, s.plannedPosition)
        >= replanDistance * replanDistance;
    if (moved) s.blocked = false;

    bool publishedThisUpdate = false;
    const auto failTarget = [&]
    {
        // Keep published coverage intact and invalidate both a retained and an
        // in-flight successor. Recovery must start from the real coverage.
        if (s.preparedPlan) ++m_stats.stalePlansDiscarded;
        s.abandonTarget();
        s.blocked = true;
        s.plannedPosition = cameraPosition;
        m_stats.resourceLimited = true;
    };
    if (failed) failTarget();

    const auto collectPlan = [&]
    {
        LodPlanResult plan;
        if (!s.planner->poll(plan)) return;
        m_stats.lastPlanningMilliseconds = plan.milliseconds;
        if (s.discardPlannerResult)
        {
            s.discardPlannerResult = false;
            ++m_stats.stalePlansDiscarded;
            return;
        }
        s.preparedPlan = Impl::PreparedPlan{std::move(plan), s.plannerLookahead};
    };
    const auto discardStalePlan = [&]
    {
        if (!s.preparedPlan) return;
        const auto& prepared = *s.preparedPlan;
        const auto revision = s.visibleRevision + (s.active ? 1u : 0u);
        const bool obsoleteCamera = prepared.lookahead && distanceSquared(
            cameraPosition, prepared.result.requestPosition) >= replanDistance * replanDistance;
        if (prepared.result.coverageRevision != revision || obsoleteCamera || s.blocked)
        {
            s.preparedPlan.reset();
            ++m_stats.stalePlansDiscarded;
        }
        // Only speculation is rejected for camera movement. A normal request
        // always makes bounded progress, even if the camera moves faster than
        // the worker can plan; otherwise continuous travel could starve forever.
    };
    const auto publishReady = [&]
    {
        if (!s.active || !std::all_of(s.target.begin(), s.target.end(),
            [&s](const ChunkKey& key) { return s.ready.count(key) != 0; })) return false;
        // Publish the whole local split/merge and transition neighbours. The
        // prepared successor cannot submit GPU work before this revision exists.
        s.visibleSelection = std::move(s.transaction.next);
        s.visibleKeys.clear();
        s.visible.clear();
        s.visible.reserve(s.visibleSelection.size());
        s.seaTiles.clear();
        s.seaTiles.reserve(s.visibleSelection.size());
        s.shadowCasterBounds.clear();
        s.shadowCasterBounds.reserve(s.visibleSelection.size());
        for (const auto& chunk : s.visibleSelection)
        {
            const ChunkKey key{chunk.origin, chunk.lod, chunk.transitionMask};
            s.visibleKeys.insert(key);
            const auto& ready = s.ready.at(key);
            const auto& mesh = ready.mesh;
            SeaTile seaTile;
            if (seaTileForChunk(chunk, s.lod.cells, m_config.cellSize,
                m_config.field.seaLevel,
                ready.hasMarineSurface, seaTile))
                s.seaTiles.push_back(seaTile);
            if (mesh.vertexCount)
            {
                s.visible.push_back(mesh);
                const glm::dvec3 minimum(mesh.origin[0], mesh.origin[1], mesh.origin[2]);
                s.shadowCasterBounds.push_back({minimum, minimum + static_cast<double>(mesh.extent)});
            }
        }
        ++s.visibleRevision;
        s.shadowBoundsRevision = Ge::nextShadowCasterBoundsRevision();
        ++m_stats.publishedLodGroups;
        publishedThisUpdate = true;
        s.transaction = {};
        s.active = false;
        s.wanted.clear();
        s.target.clear();
        s.collisionDirty = true;
        s.discardUnused();
        s.gpu.publishObjectCoverage(s.visibleSelection,m_seed);
        return true;
    };
    const auto activatePrepared = [&]
    {
        if (s.active || !s.preparedPlan) return;
        auto prepared = std::move(*s.preparedPlan);
        s.preparedPlan.reset();
        auto& plan = prepared.result;
        if (prepared.lookahead) ++m_stats.lookaheadPlansUsed;
        s.plannedPosition = plan.selectionPosition;
        s.planned = true;
        s.desiredCount = plan.desiredCount;
        if (plan.selected) ++m_stats.lodSelections;
        if (!plan.success)
        {
            m_lastError = plan.error;
            s.blocked = true;
            s.plannedPosition = cameraPosition;
            m_stats.resourceLimited = true;
            return;
        }
        s.converged = plan.transaction.kind == LodWorkKind::None;
        s.active = !s.converged;
        s.transaction = std::move(plan.transaction);
        s.target.clear();
        s.wanted.clear();
        for (auto index : s.transaction.nextIndices)
        {
            const auto& chunk = s.transaction.next[index];
            const ChunkKey key{chunk.origin, chunk.lod, chunk.transitionMask};
            s.target.push_back(key);
            s.wanted.insert(key);
        }
        m_stats.largestLodTransaction = std::max(m_stats.largestLodTransaction,
            static_cast<std::uint32_t>(s.target.size()));
        m_stats.resourceLimited = false;
        m_lastError.clear();
    };
    const auto requestPlan = [&]
    {
        const bool selectionMoved = !s.planned || distanceSquared(cameraPosition, s.plannedPosition)
            >= replanDistance * replanDistance;
        if (s.blocked || s.preparedPlan || s.planner->busy()
            || (!s.active && s.converged && !selectionMoved)) return;
        // The CPU only reads this copy; current GPU meshes and visible coverage
        // remain unchanged. One request/result bounds memory and obsolete work.
        const bool ahead = s.active;
        if (s.planner->request(ahead ? s.transaction.next : s.visibleSelection,
            cameraPosition, s.visibleRevision + (ahead ? 1u : 0u)))
        {
            s.plannerLookahead = ahead;
            s.discardPlannerResult = false;
        }
    };

    std::uint32_t dispatched = 0;
    unsigned schedulingSteps = 0;
    bool handoffRecorded = false;
    // One extra nonblocking poll can advance a quick count to emission, or
    // refill slots released by empty/cache jobs, without waiting a full frame.
    // Both passes share the original submission and four-publication budgets;
    // there are at most two polls/dispatches, never a fence-watching loop.
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        for (; schedulingSteps < 4; )
        {
            ++schedulingSteps;
            if (!failed) publishReady();
            collectPlan();
            discardStalePlan();
            if (!failed && !s.blocked) activatePrepared();
            requestPlan();
            if (!s.active || !std::all_of(s.target.begin(), s.target.end(),
                [&s](const ChunkKey& key) { return s.ready.count(key) != 0; })) break;
        }
        const auto before = dispatched;
        if (!failed) for (const auto& key : s.target)
        {
            if (s.ready.count(key) || s.submitted.count(key)) continue;
            if (dispatched >= m_config.maxChunksPerFrame
                || s.gpu.stats().pendingJobs >= m_config.maxInFlight) break;
            if (!computeState) computeState.emplace();
            ChunkRequest request;
            request.origin = key.origin;
            request.lod = key.lod;
            request.transitionMask = key.mask;
            request.seed = m_seed;
            const auto token = s.nextToken++;
            if (!s.gpu.submit(request, token))
            {
                // A cancelled job can still occupy a fenced slot; retry when free.
                if (!s.gpu.lastError().empty())
                {
                    m_lastError = s.gpu.lastError();
                    failed = true;
                }
                break;
            }
            s.pending.emplace(token, key);
            s.submitted.insert(key);
            ++dispatched;
        }
        if (dispatched != before && !failed)
        {
            s.gpu.dispatchQueued();
            if (publishedThisUpdate && !handoffRecorded)
            { ++m_stats.sameFrameHandoffs; handoffRecorded = true; }
        }
        if (pass || failed || !s.gpu.stats().pendingJobs) break;
        collectCompletions();
        // Advancing count -> emission needs no CPU publication work. Only
        // actual completions can release slots or make a transaction ready.
        if (failed || s.completions.empty()) break;
    }
    if (failed && s.active) failTarget();
    const auto queued = static_cast<std::uint32_t>(std::count_if(s.target.begin(), s.target.end(),
        [&s](const ChunkKey& key) { return !s.ready.count(key) && !s.submitted.count(key); }));

    if (m_config.collisionEnabled)
    {
        const bool changed = s.collisionDirty;
        if (changed)
        {
            s.collisionCandidates.clear();
            s.collisionKeys.clear();
            const auto addCollision = [&s](const ChunkKey& key)
            {
                if (key.lod != 0) return;
                const auto found = s.ready.find(key);
                if (found == s.ready.end() || found->second.mesh.vertexCount == 0) return;
                ChunkKey spatialKey = key;
                spatialKey.mask = 0;
                if (s.collisionKeys.insert(spatialKey).second)
                    s.collisionCandidates.push_back(found->second.mesh);
            };
            for (const auto& key : s.target) addCollision(key);
            for (const auto& chunk : s.visibleSelection)
                addCollision({chunk.origin, chunk.lod, chunk.transitionMask});
            s.collisionDirty = false;
        }
        const auto before = s.collision.stats();
        if (changed || !s.collisionUpdated || cameraPosition != s.collisionPosition
            || before.pendingReadbacks || before.pendingBuilds)
        {
            s.collision.update(cameraPosition, s.collisionCandidates, s.gpu);
            s.collisionPosition = cameraPosition;
            s.collisionUpdated = true;
        }
    }
    const auto collision = s.collision.stats();
    m_stats.collisionChunks = collision.residentChunks;
    m_stats.pendingCollisionJobs = collision.pendingReadbacks + collision.pendingBuilds;
    const bool collisionFailed = collision.failedChunks > m_stats.failedCollisionJobs;
    m_stats.failedCollisionJobs = collision.failedChunks;
    if (collision.capacityLimited || collisionFailed)
    {
        m_stats.resourceLimited = true;
        if (!s.collision.lastError().empty()) m_lastError = s.collision.lastError();
    }
    const auto gpu = s.gpu.stats();
    m_stats.selectedChunks = static_cast<std::uint32_t>(s.desiredCount);
    m_stats.visibleChunks = static_cast<std::uint32_t>(s.visible.size());
    m_stats.queuedChunks = queued;
    m_stats.pendingJobs = gpu.pendingJobs;
    m_stats.residentChunks = gpu.residentChunks;
    m_stats.residentVertices = gpu.residentVertices;
    m_stats.allocatedBytes = gpu.allocatedBytes;
    m_stats.budgetBytes = gpu.budgetBytes;
    m_stats.completedJobs = gpu.completedJobs;
    m_stats.emptyJobs = gpu.emptyJobs;
    m_stats.failedJobs = gpu.failedJobs;
    m_stats.lastGenerationMilliseconds = gpu.lastGenerationMilliseconds;
    m_stats.transientFieldBytes = gpu.transientFieldBytes;
    m_stats.generation = gpu;
    m_stats.drawListUploads = gpu.drawListUploads;
    m_stats.drawListUploadBytes = gpu.drawListUploadBytes;
    m_stats.cpuPlanning = s.planner->busy();
    m_stats.lookaheadReady = s.preparedPlan && s.preparedPlan->lookahead;
    m_stats.plannerWaiting = !s.active && !s.blocked && gpu.pendingJobs == 0 && m_stats.cpuPlanning;
    if (m_stats.plannerWaiting) ++m_stats.planningWaitFrames;
    m_stats.rebuilding = s.active || m_stats.cpuPlanning || s.preparedPlan
        || (!s.converged && !s.blocked);
    m_stats.generationBlocked = s.blocked;
    m_stats.coverageComplete = s.planned && s.converged && !s.blocked
        && !m_stats.rebuilding && gpu.pendingJobs == 0 && queued == 0
        && distanceSquared(cameraPosition, s.plannedPosition) < replanDistance * replanDistance;
    const auto now = std::chrono::steady_clock::now();
    if (m_stats.rebuilding && !s.timingCoverage)
    { s.coverageStart = now; s.timingCoverage = true; }
    if (s.timingCoverage)
    {
        m_stats.coverageMilliseconds = std::chrono::duration<double,std::milli>(now-s.coverageStart).count();
        if (!m_stats.rebuilding) s.timingCoverage = false;
    }
    m_stats.pendingLodGroups = s.active ? 1u : 0u;
}

void ProceduralGeneration::drawRenderer()
{
    if (!m_impl || m_impl->visible.empty()) return;
    auto& s = *m_impl;
    auto* camera = s.engine->cameraManager ? s.engine->cameraManager->getCurrentCamera() : nullptr;
    if (!camera) return;
    const auto* uniform = camera->getUniformCamera();
    if (s.cameraVisibility.update(camera, uniform->view, uniform->proj,
        camera->getPosition(), s.visibleRevision, 8.0f * m_config.cellSize))
    {
        s.cameraMeshes.clear();
        for (const auto& mesh : s.visible)
            if (intersectsFrustum(s.cameraVisibility.frustum(), mesh.origin, mesh.extent))
                s.cameraMeshes.push_back(mesh);
        ++m_stats.cameraCullUpdates;
    }
    m_stats.cameraDrawChunks = static_cast<std::uint32_t>(s.cameraMeshes.size());
    m_stats.cameraCulledChunks = static_cast<std::uint32_t>(s.visible.size() - s.cameraMeshes.size());
    if (s.cameraMeshes.empty()) return;
    TerrainRenderStateGuard state;
    TerrainMaterialStateGuard materialState;
    s.refreshAlbedoGamma();
    glUseProgram(s.render->getProgram());
    glBindBufferBase(GL_UNIFORM_BUFFER, 6, s.materialBuffer);
    s.gpu.drawMany(s.cameraMeshes, 0);
    const auto gpuStats = s.gpu.stats();
    m_stats.drawListUploads = gpuStats.drawListUploads;
    m_stats.drawListUploadBytes = gpuStats.drawListUploadBytes;
}

void ProceduralGeneration::drawShadowRenderer(unsigned int shadowIndex)
{
    if (!m_impl || m_impl->visible.empty()) return;
    auto& s = *m_impl;
    glm::mat4 lightMatrix;
    if (!s.engine->lightManager || !s.engine->lightManager->getShadowMatrix(shadowIndex, lightMatrix)) return;
    auto& visibility = s.selectShadow(shadowIndex, lightMatrix, m_stats);
    m_stats.shadowDrawChunks += static_cast<std::uint32_t>(visibility.meshes().size());
    m_stats.shadowCulledChunks += static_cast<std::uint32_t>(s.visible.size() - visibility.meshes().size());
    if (visibility.meshes().empty()) return;
    TerrainRenderStateGuard state;
    glUseProgram(s.shadow->getProgram());
    glUniform1i(s.shadowIndex, static_cast<GLint>(shadowIndex));
    s.gpu.drawMany(visibility.meshes(), shadowIndex + 1u);
    const auto gpuStats = s.gpu.stats();
    m_stats.drawListUploads = gpuStats.drawListUploads;
    m_stats.drawListUploadBytes = gpuStats.drawListUploadBytes;
}

bool ProceduralGeneration::isInitialized() const noexcept { return m_impl != nullptr; }
std::uint64_t ProceduralGeneration::shadowCasterBoundsRevision() const
{ return m_impl ? m_impl->shadowBoundsRevision : 0; }
std::uint64_t ProceduralGeneration::shadowContentRevision() const
{
    // The immutable shadow pipeline and generated mesh buffers live for one
    // Impl lifetime. A fresh token is issued on restart and every publication.
    // Surface microdetail is shading-only and does not alter shadow geometry.
    return m_impl ? m_impl->shadowBoundsRevision : 0;
}
std::uint64_t ProceduralGeneration::shadowLayerContentRevision(unsigned layer, const glm::mat4& clip) const
{
    // Shadow program and immutable mesh allocations live for this Impl. Every
    // restart and changed/recycled selected allocation receives a unique token.
    return m_impl ? m_impl->selectShadow(layer, clip, m_stats).contentRevision() : 0;
}
void ProceduralGeneration::appendShadowCasterBounds(std::vector<Ge::ShadowCasterBounds>& output) const
{
    if (m_impl) output.insert(output.end(), m_impl->shadowCasterBounds.begin(), m_impl->shadowCasterBounds.end());
}
const ProceduralGeneration::Stats& ProceduralGeneration::getStats() const noexcept { return m_stats; }
const std::string& ProceduralGeneration::getLastError() const noexcept { return m_lastError; }
const ProceduralGeneration::Config& ProceduralGeneration::getConfig() const noexcept { return m_config; }

void ProceduralGeneration::getVisibleLodChunks(std::vector<ProceduralTerrain::LodChunk>& output) const
{
    if (m_impl) output = m_impl->visibleSelection;
    else output.clear();
}

const std::vector<ProceduralTerrain::SeaTile>& ProceduralGeneration::getSeaTiles() const noexcept
{
    static const std::vector<ProceduralTerrain::SeaTile> empty;
    return m_impl ? m_impl->seaTiles : empty;
}

std::uint64_t ProceduralGeneration::getCoverageRevision() const noexcept
{
    return m_impl ? m_impl->visibleRevision : 0;
}

unsigned int ProceduralGeneration::getObjectInstanceBuffer() const noexcept
{ return m_impl ? m_impl->gpu.objectInstanceBuffer() : 0; }
const std::vector<ProceduralTerrain::ObjectBlock>& ProceduralGeneration::getObjectBlocks() const noexcept
{
    static const std::vector<ProceduralTerrain::ObjectBlock> empty;
    return m_impl ? m_impl->gpu.objectBlocks() : empty;
}
ProceduralTerrain::ObjectPlacementStats ProceduralGeneration::getObjectPlacementStats() const noexcept
{ return m_impl ? m_impl->gpu.objectStats() : ProceduralTerrain::ObjectPlacementStats{}; }
const std::string& ProceduralGeneration::getObjectPlacementError() const noexcept
{
    static const std::string empty;
    return m_impl ? m_impl->gpu.objectError() : empty;
}
