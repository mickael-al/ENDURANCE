#include "glcore.hpp"
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainLod.hpp"
#include "ProceduralTerrainFrustum.hpp"
#include "ProceduralTerrainStreaming.hpp"
#include "ProceduralTerrainPlanner.hpp"
#include "ProceduralTerrainVisibility.hpp"
#include "ProceduralTerrainCollision.hpp"
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
        GLint program = 0, storage = 0;
        std::array<GLint, 6> bindings{};
        std::array<GLint64, 6> starts{}, sizes{};
        ComputeState()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &storage);
            for (GLuint i = 0; i < bindings.size(); ++i)
            {
                glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &bindings[i]);
                glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &starts[i]);
                glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &sizes[i]);
            }
        }
        ~ComputeState()
        {
            glUseProgram(program);
            for (GLuint i = 0; i < bindings.size(); ++i)
            {
                if (bindings[i] && sizes[i] > 0)
                    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, bindings[i], starts[i], sizes[i]);
                else glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, bindings[i]);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, storage);
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
    std::unordered_map<ChunkKey, MeshHandle, KeyHash> ready;
    std::unordered_map<std::uint64_t, ChunkKey> pending;
    KeySet submitted, wanted, visibleKeys, collisionKeys;
    std::unique_ptr<LodPlanner> planner;
    LodTransaction transaction;
    std::vector<LodChunk> visibleSelection;
    CameraVisibilityCache cameraVisibility;
    struct ShadowVisibility
    {
        ShadowVisibilityCache cache;
        std::vector<MeshHandle> meshes;
    };
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
    std::size_t desiredCount = 0;
    bool planned = false, active = false, converged = false, blocked = false;
    bool collisionDirty = true, collisionUpdated = false;
    GLint detailStrength = -1, detailScale = -1, seed = -1, debug = -1, shadowIndex = -1;
    GLint biomeOverride = -1, seaLevel = -1, seaBlendWidth = -1;

    ~Impl()
    {
        if (registered && pipelines) pipelines->removeCustomRenderer(registered);
        planner.reset();
        collision.shutdown();
        gpu.shutdown();
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
        auto destroy = [this](Ge::GraphiquePipeline*& p)
        {
            if (!p) return;
            auto* pair = p->getShaderPair();
            pipelines->destroyPipeline(p);
            delete pair;
            p = nullptr;
        };
        destroy(shadow);
        destroy(render);
    }

    void discardUnused()
    {
        for (auto it = ready.begin(); it != ready.end();)
        {
            if (visibleKeys.count(it->first) || wanted.count(it->first)) { ++it; continue; }
            gpu.release(it->second);
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
        collisionDirty = true;
        discardUnused();
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
        || config.maxInFlight == 0 || config.maxInFlight > 8
        || config.maxSelectedChunks == 0 || config.maxSelectedChunks > 32768
        || config.maxResidentChunks < config.maxSelectedChunks
        || !finite(config.collisionRadius) || config.collisionRadius < 0.0f
        || (config.collisionEnabled && config.collisionRadius == 0.0f)
        || config.collisionRadius > config.viewDistance
        || !finite(config.microDetailStrength) || config.microDetailStrength < 0.0f || config.microDetailStrength > 16.0f
        || !finite(config.microDetailScale) || config.microDetailScale <= 0.0f || config.microDetailScale > 1000000.0f
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
    gpuConfig.shaderDirectory = config.shaderDirectory;
    gpuConfig.field = config.field;
    if (!impl->gpu.initialize(gpuConfig, m_lastError)) return false;

    const auto path = [&config](const char* name) { return config.shaderDirectory + "/" + name; };
    impl->render = impl->pipelines->createPipeline(path("render.fs.glsl"), path("render.vs.glsl"));
    impl->shadow = impl->pipelines->createPipeline(path("shadow.fs.glsl"), path("shadow.vs.glsl"));
    for (auto* pipeline : {impl->render, impl->shadow})
    {
        GLint linked = GL_FALSE;
        if (pipeline && glIsProgram(pipeline->getProgram()))
            glGetProgramiv(pipeline->getProgram(), GL_LINK_STATUS, &linked);
        if (!linked)
        {
            m_lastError = "Terrain render/shadow shader failed to link; see shader log.";
            return false;
        }
    }
    const GLuint render = impl->render->getProgram();
    impl->detailStrength = glGetUniformLocation(render, "microDetailStrength");
    impl->detailScale = glGetUniformLocation(render, "microDetailScale");
    impl->seed = glGetUniformLocation(render, "worldSeed");
    impl->debug = glGetUniformLocation(render, "debugLod");
    impl->biomeOverride = glGetUniformLocation(render, "terrainBiomeOverride");
    impl->seaLevel = glGetUniformLocation(render, "terrainSeaLevel");
    impl->seaBlendWidth = glGetUniformLocation(render, "terrainSeaBlendWidth");
    impl->shadowIndex = glGetUniformLocation(impl->shadow->getProgram(), "offsetShadow");

    impl->lod.baseSpacing = config.cellSize;
    impl->lod.maxLod = config.maxLod;
    impl->lod.viewDistance = config.viewDistance;
    impl->lod.splitDistanceFactor = config.lodDistanceFactor;
    impl->lod.maxLeaves = config.maxSelectedChunks;
    impl->lod.fullResolutionRadius = config.collisionEnabled ? config.collisionRadius : 0.0f;
    const auto bounds = fieldHeightBounds(config.field);
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
    impl->pipelines->addCustomRenderer(this);
    impl->registered = this;
    m_impl = std::move(impl);
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
    // poll() can emit geometry only while compute jobs are in flight.
    std::optional<ComputeState> computeState;
    if (s.gpu.stats().pendingJobs) computeState.emplace();
    s.completions.clear();
    s.gpu.poll(s.completions);
    bool failed = false;
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
            s.ready.emplace(key, result.mesh);
            s.collisionDirty = true;
        }
        else if (result.status == CompletionStatus::Failed)
        {
            m_lastError = result.error.empty() ? "GPU terrain generation failed." : result.error;
            failed = true;
        }
    }

    const auto distanceSquared = [](const glm::vec3& a, const glm::vec3& b)
    {
        const auto delta = glm::dvec3(a) - glm::dvec3(b);
        return glm::dot(delta, delta);
    };
    const double replanDistance = 32.0 * m_config.cellSize;
    const bool moved = !s.planned || distanceSquared(cameraPosition, s.plannedPosition)
        >= replanDistance * replanDistance;
    if (moved) s.blocked = false;

    LodPlanResult plan;
    if (s.planner->poll(plan) && plan.coverageRevision == s.visibleRevision)
    {
        m_stats.lastPlanningMilliseconds = plan.milliseconds;
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
        }
        else
        {
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
        }
    }

    std::uint32_t queued = 0, dispatched = 0;
    if (!failed) for (const auto& key : s.target)
    {
        if (s.ready.count(key) || s.submitted.count(key)) continue;
        ++queued;
        if (dispatched >= m_config.maxChunksPerFrame
            || s.gpu.stats().pendingJobs >= m_config.maxInFlight) continue;
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
        --queued;
    }
    if (failed)
    {
        // Keep the published coverage intact, with no automatic failure loop.
        s.abandonTarget();
        s.blocked = true;
        s.plannedPosition = cameraPosition;
        m_stats.resourceLimited = true;
        queued = 0;
    }
    else if (s.active && std::all_of(s.target.begin(), s.target.end(),
        [&s](const ChunkKey& key) { return s.ready.count(key) != 0; }))
    {
        // One local split/merge (and the affected transition faces) commits as
        // a whole. A nearby refinement cannot wait for distant downgrades.
        s.visibleSelection = std::move(s.transaction.next);
        s.visibleKeys.clear();
        s.visible.clear();
        s.visible.reserve(s.visibleSelection.size());
        for (const auto& chunk : s.visibleSelection)
        {
            const ChunkKey key{chunk.origin, chunk.lod, chunk.transitionMask};
            s.visibleKeys.insert(key);
            const auto& mesh = s.ready.at(key);
            if (mesh.vertexCount) s.visible.push_back(mesh);
        }
        ++s.visibleRevision;
        ++m_stats.publishedLodGroups;
        s.transaction = {};
        s.active = false;
        s.wanted.clear();
        s.target.clear();
        s.collisionDirty = true;
        s.discardUnused();
    }

    // Only one bounded GPU transaction is frozen at a time. The next CPU job
    // snapshots the last published octree and the latest camera position; no
    // queue of obsolete destinations, no cancelling unfinished work each step.
    const bool selectionMoved = !s.planned || distanceSquared(cameraPosition, s.plannedPosition)
        >= replanDistance * replanDistance;
    if (!s.active && !s.blocked && (!s.converged || selectionMoved) && !s.planner->busy())
        s.planner->request(s.visibleSelection, cameraPosition, s.visibleRevision);

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
                if (found == s.ready.end() || found->second.vertexCount == 0) return;
                ChunkKey spatialKey = key;
                spatialKey.mask = 0;
                if (s.collisionKeys.insert(spatialKey).second)
                    s.collisionCandidates.push_back(found->second);
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
    m_stats.drawListUploads = gpu.drawListUploads;
    m_stats.drawListUploadBytes = gpu.drawListUploadBytes;
    m_stats.rebuilding = s.active || s.planner->busy() || (!s.converged && !s.blocked);
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
    glUseProgram(s.render->getProgram());
    glUniform1f(s.detailStrength, m_config.microDetailStrength);
    glUniform1f(s.detailScale, m_config.microDetailScale);
    glUniform1ui(s.seed, m_seed);
    glUniform1i(s.debug, m_config.debugLod ? 1 : 0);
    glUniform1i(s.biomeOverride, m_config.field.biomeOverride);
    glUniform1f(s.seaLevel, m_config.field.seaLevel);
    glUniform1f(s.seaBlendWidth, m_config.field.seaBlendWidth);
    s.gpu.drawMany(s.cameraMeshes, 0);
    m_stats.drawListUploads = s.gpu.stats().drawListUploads;
    m_stats.drawListUploadBytes = s.gpu.stats().drawListUploadBytes;
}

void ProceduralGeneration::drawShadowRenderer(unsigned int shadowIndex)
{
    if (!m_impl || m_impl->visible.empty()) return;
    auto& s = *m_impl;
    glm::mat4 lightMatrix;
    if (!s.engine->lightManager || !s.engine->lightManager->getShadowMatrix(shadowIndex, lightMatrix)) return;
    auto& visibility = s.shadowVisibility[shadowIndex];
    if (visibility.cache.update(lightMatrix, s.visibleRevision))
    {
        visibility.meshes.clear();
        for (const auto& mesh : s.visible)
            if (intersectsFrustum(visibility.cache.frustum(), mesh.origin, mesh.extent))
                visibility.meshes.push_back(mesh);
        ++m_stats.shadowCullUpdates;
    }
    m_stats.shadowDrawChunks += static_cast<std::uint32_t>(visibility.meshes.size());
    m_stats.shadowCulledChunks += static_cast<std::uint32_t>(s.visible.size() - visibility.meshes.size());
    if (visibility.meshes.empty()) return;
    TerrainRenderStateGuard state;
    glUseProgram(s.shadow->getProgram());
    glUniform1i(s.shadowIndex, static_cast<GLint>(shadowIndex));
    s.gpu.drawMany(visibility.meshes, shadowIndex + 1u);
    m_stats.drawListUploads = s.gpu.stats().drawListUploads;
    m_stats.drawListUploadBytes = s.gpu.stats().drawListUploadBytes;
}

bool ProceduralGeneration::isInitialized() const noexcept { return m_impl != nullptr; }
const ProceduralGeneration::Stats& ProceduralGeneration::getStats() const noexcept { return m_stats; }
const std::string& ProceduralGeneration::getLastError() const noexcept { return m_lastError; }
const ProceduralGeneration::Config& ProceduralGeneration::getConfig() const noexcept { return m_config; }

void ProceduralGeneration::getVisibleLodChunks(std::vector<ProceduralTerrain::LodChunk>& output) const
{
    if (m_impl) output = m_impl->visibleSelection;
    else output.clear();
}
