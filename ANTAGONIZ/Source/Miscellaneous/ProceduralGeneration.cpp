#include "glcore.hpp"
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainLod.hpp"
#include "ProceduralTerrainFrustum.hpp"
#include "ProceduralTerrainStreaming.hpp"
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
    struct PendingLodGroup
    {
        std::vector<ChunkKey> remove, add;
    };
    struct CameraRange
    {
        Ge::Camera* camera;
        float originalFar, appliedFar;
    };
    std::vector<CameraRange> cameraRanges;
    std::vector<PendingLodGroup> groups;
    std::vector<ChunkKey> target;
    std::vector<MeshHandle> visible, culled, collisionCandidates;
    std::vector<Completion> completions;
    glm::vec3 plannedPosition{};
    std::uint64_t nextToken = 1;
    bool planned = false, rebuilding = false;
    GLint detailStrength = -1, detailScale = -1, seed = -1, debug = -1, shadowIndex = -1;
    GLint biomeOverride = -1, seaLevel = -1, seaBlendWidth = -1;

    ~Impl()
    {
        if (registered && pipelines) pipelines->removeCustomRenderer(registered);
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
        groups.clear();
        rebuilding = false;
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
    ComputeState computeState;
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
            s.ready.emplace(key, result.mesh);
        else if (result.status == CompletionStatus::Failed)
        {
            m_lastError = result.error.empty() ? "GPU terrain generation failed." : result.error;
            failed = true;
        }
    }
    if (failed)
    {
        // Preserve the last published coverage. Never publish an incomplete
        // group, silently drop geometry or repeatedly retry an exhausted pool.
        s.abandonTarget();
        s.plannedPosition = cameraPosition;
        m_stats.resourceLimited = true;
    }

    const glm::vec3 delta = cameraPosition - s.plannedPosition;
    const double distanceSquared = static_cast<double>(delta.x) * delta.x
        + static_cast<double>(delta.y) * delta.y + static_cast<double>(delta.z) * delta.z;
    const double replanDistance = 32.0 * m_config.cellSize;
    if (!s.planned || distanceSquared >= replanDistance * replanDistance)
    {
        std::vector<LodChunk> selection;
        std::string error;
        s.planned = true;
        s.plannedPosition = cameraPosition;
        if (selectLodChunks(cameraPosition, s.lod, selection, error))
        {
            ++m_stats.lodSelections;
            // Compare with what is actually on screen, which may already contain
            // completed groups from the previous destination. Keep common jobs.
            std::vector<LodChunk> previous;
            previous.reserve(s.visibleKeys.size());
            for (const auto& key : s.visibleKeys)
                previous.push_back({key.origin, key.lod, key.mask});
            std::sort(previous.begin(), previous.end(), [](const LodChunk& a, const LodChunk& b)
            {
                if (a.origin != b.origin) return a.origin < b.origin;
                if (a.lod != b.lod) return a.lod < b.lod;
                return a.transitionMask < b.transitionMask;
            });
            s.target.clear();
            s.wanted.clear();
            s.target.reserve(selection.size());
            for (const auto& chunk : selection)
            {
                const ChunkKey key{chunk.origin, chunk.lod, chunk.transitionMask};
                s.target.push_back(key);
                s.wanted.insert(key);
            }
            for (auto job = s.pending.begin(); job != s.pending.end();)
            {
                if (s.wanted.count(job->second)) { ++job; continue; }
                s.gpu.cancel(job->first);
                s.submitted.erase(job->second);
                job = s.pending.erase(job);
            }
            s.groups.clear();
            if (previous.empty() && !selection.empty())
            {
                // The first coverage is published whole. Later replacements are
                // independent, crack-free groups rather than one global barrier.
                Impl::PendingLodGroup group;
                group.add = s.target;
                s.groups.push_back(std::move(group));
            }
            else
            {
                const auto changes = groupLodChanges(previous, selection, s.lod.cells);
                for (const auto& change : changes)
                {
                    Impl::PendingLodGroup group;
                    for (auto index : change.previousIndices)
                    {
                        const auto& old = previous[index];
                        group.remove.push_back({old.origin, old.lod, old.transitionMask});
                    }
                    for (auto index : change.nextIndices) group.add.push_back(s.target[index]);
                    s.groups.push_back(std::move(group));
                }
            }
            s.rebuilding = !s.groups.empty();
            m_stats.resourceLimited = false;
            m_lastError.clear();
            s.discardUnused();
        }
        else
        {
            m_stats.resourceLimited = true;
            m_lastError = error;
        }
    }

    std::uint32_t queued = 0, dispatched = 0;
    for (const auto& key : s.target)
    {
        if (s.ready.count(key)) continue;
        if (s.submitted.count(key)) continue;
        ++queued;
        if (dispatched >= m_config.maxChunksPerFrame
            || s.gpu.stats().pendingJobs >= m_config.maxInFlight) continue;
        ChunkRequest request;
        request.origin = key.origin;
        request.lod = key.lod;
        request.transitionMask = key.mask;
        request.seed = m_seed;
        const auto token = s.nextToken++;
        if (!s.gpu.submit(request, token))
        {
            // A cancelled job can still occupy a fenced work slot. Only treat
            // a submit error as fatal when the backend reports one.
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
    if (failed && s.rebuilding)
    {
        s.abandonTarget();
        m_stats.resourceLimited = true;
        queued = 0;
    }
    else
    {
        bool changed = false;
        for (auto group = s.groups.begin(); group != s.groups.end();)
        {
            const bool complete = std::all_of(group->add.begin(), group->add.end(),
                [&s](const ChunkKey& key) { return s.ready.count(key) != 0; });
            if (!complete) { ++group; continue; }
            // Overlap and incompatible boundary dependencies belong to the same
            // group, so every intermediate visible octree remains consistent.
            for (const auto& key : group->remove) s.visibleKeys.erase(key);
            for (const auto& key : group->add) s.visibleKeys.insert(key);
            group = s.groups.erase(group);
            ++m_stats.publishedLodGroups;
            changed = true;
        }
        if (changed)
        {
            s.visible.clear();
            s.visible.reserve(s.visibleKeys.size());
            for (const auto& key : s.visibleKeys)
            {
                const auto& mesh = s.ready.at(key);
                if (mesh.vertexCount) s.visible.push_back(mesh);
            }
        }
        s.rebuilding = !s.groups.empty();
        if (!s.rebuilding)
        {
            s.wanted.clear();
            s.target.clear();
        }
        if (changed || !s.rebuilding) s.discardUnused();
    }

    if (m_config.collisionEnabled)
    {
        // Nearby physics need not wait for all distant visual LODs. Cached
        // colliders outlive these visual handles and have their own radius.
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
        // A changed transition mask can give two versions of one spatial
        // chunk. Prefer the new one, then fill any gaps with the visible set.
        for (const auto& key : s.target) addCollision(key);
        for (const auto& key : s.visibleKeys) addCollision(key);
        s.collision.update(cameraPosition, s.collisionCandidates, s.gpu);
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
    m_stats.selectedChunks = static_cast<std::uint32_t>(s.rebuilding ? s.target.size() : s.visibleKeys.size());
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
    m_stats.rebuilding = s.rebuilding;
    m_stats.pendingLodGroups = static_cast<std::uint32_t>(s.groups.size());
}

void ProceduralGeneration::drawRenderer()
{
    if (!m_impl || m_impl->visible.empty()) return;
    auto& s = *m_impl;
    s.culled.clear();
    auto* camera = s.engine->cameraManager ? s.engine->cameraManager->getCurrentCamera() : nullptr;
    if (!camera) return;
    const auto frustum = makeFrustum(camera->getProjectionMatrix() * camera->getViewMatrix());
    for (const auto& mesh : s.visible)
        if (intersectsFrustum(frustum, mesh.origin, mesh.extent)) s.culled.push_back(mesh);
    m_stats.cameraDrawChunks = static_cast<std::uint32_t>(s.culled.size());
    m_stats.cameraCulledChunks = static_cast<std::uint32_t>(s.visible.size() - s.culled.size());
    if (s.culled.empty()) return;
    TerrainRenderStateGuard state;
    glUseProgram(s.render->getProgram());
    glUniform1f(s.detailStrength, m_config.microDetailStrength);
    glUniform1f(s.detailScale, m_config.microDetailScale);
    glUniform1ui(s.seed, m_seed);
    glUniform1i(s.debug, m_config.debugLod ? 1 : 0);
    glUniform1i(s.biomeOverride, m_config.field.biomeOverride);
    glUniform1f(s.seaLevel, m_config.field.seaLevel);
    glUniform1f(s.seaBlendWidth, m_config.field.seaBlendWidth);
    s.gpu.drawMany(s.culled);
}

void ProceduralGeneration::drawShadowRenderer(unsigned int shadowIndex)
{
    if (!m_impl || m_impl->visible.empty()) return;
    auto& s = *m_impl;
    glm::mat4 lightMatrix;
    if (!s.engine->lightManager || !s.engine->lightManager->getShadowMatrix(shadowIndex, lightMatrix)) return;
    const auto frustum = makeFrustum(lightMatrix);
    s.culled.clear();
    for (const auto& mesh : s.visible)
        if (intersectsFrustum(frustum, mesh.origin, mesh.extent)) s.culled.push_back(mesh);
    m_stats.shadowDrawChunks += static_cast<std::uint32_t>(s.culled.size());
    m_stats.shadowCulledChunks += static_cast<std::uint32_t>(s.visible.size() - s.culled.size());
    if (s.culled.empty()) return;
    TerrainRenderStateGuard state;
    glUseProgram(s.shadow->getProgram());
    glUniform1i(s.shadowIndex, static_cast<GLint>(shadowIndex));
    s.gpu.drawMany(s.culled);
}

bool ProceduralGeneration::isInitialized() const noexcept { return m_impl != nullptr; }
const ProceduralGeneration::Stats& ProceduralGeneration::getStats() const noexcept { return m_stats; }
const std::string& ProceduralGeneration::getLastError() const noexcept { return m_lastError; }
const ProceduralGeneration::Config& ProceduralGeneration::getConfig() const noexcept { return m_config; }
