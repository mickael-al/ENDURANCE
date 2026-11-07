#include "ProceduralTerrain.hpp"
#define CLEAR_REGISTRY
#include "ProceduralGeneration.hpp"
#include "Camera.hpp"
#include "Editor.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "Debug.hpp"
#include <algorithm>
#include <cmath>
#include <exception>

// Register in this translation unit only, so including the component header
// cannot create duplicate factories or duplicate JSON specializations.
REGISTER(Behaviour, ProceduralTerrainComponent, seed, cellSize, maxLod,
    viewDistance, lodDistanceFactor, maxChunksPerFrame, maxInFlight,
    vertexBudgetMiB, maxResidentChunks, maxSelectedChunks, baseHeight,
    heightAmplitude, heightFrequency, climateFrequency, bandThickness,
    bandVariation, bandOffset, bandOffsetVariation, detailScale,
    microDetailStrength, microDetailScale, debugLod, collisionEnabled, collisionRadius);

ProceduralTerrainComponent::ProceduralTerrainComponent() = default;

ProceduralTerrainComponent::~ProceduralTerrainComponent()
{
    stop();
}

void ProceduralTerrainComponent::start()
{
    stop();
    m_error.clear();
    m_pc = Ge::Engine::getPtrClassAddr();
    Ge::Editor* editor = Ge::Editor::instance();
    SceneData* scene = editor != nullptr ? editor->getCurrentSceneData() : nullptr;
    if (scene == nullptr || m_pc == nullptr || m_pc->cameraManager == nullptr)
    {
        m_error = "Terrain requires an active scene and camera manager.";
        Ge::Debug::Error("%s", m_error.c_str());
        return;
    }

    ProceduralGeneration::Config config;
    config.cellSize = cellSize;
    config.maxLod = maxLod;
    config.viewDistance = viewDistance;
    config.lodDistanceFactor = lodDistanceFactor;
    config.maxChunksPerFrame = maxChunksPerFrame;
    config.maxInFlight = maxInFlight;
    config.vertexBudgetMiB = vertexBudgetMiB;
    config.maxResidentChunks = maxResidentChunks;
    config.maxSelectedChunks = maxSelectedChunks;
    config.field.baseHeight = baseHeight;
    config.field.heightAmplitude = heightAmplitude;
    config.field.heightFrequency = heightFrequency;
    config.field.climateFrequency = climateFrequency;
    config.field.bandThickness = bandThickness;
    config.field.bandVariation = bandVariation;
    config.field.bandOffset = bandOffset;
    config.field.bandOffsetVariation = bandOffsetVariation;
    config.field.detailScale = detailScale;
    config.microDetailStrength = microDetailStrength;
    config.microDetailScale = microDetailScale;
    config.debugLod = debugLod;
    config.collisionEnabled = collisionEnabled;
    config.collisionRadius = collisionRadius;
    try
    {
        m_generator = std::make_unique<ProceduralGeneration>(*scene, seed);
        if (!m_generator->initialize(config))
        {
            m_error = m_generator->getLastError();
            m_generator.reset();
        }
    }
    catch (const std::exception& error)
    {
        m_error = error.what();
        m_generator.reset();
    }
    if (!m_generator)
        Ge::Debug::Error("Procedural terrain initialization failed: %s", m_error.c_str());
}

void ProceduralTerrainComponent::fixedUpdate() {}

void ProceduralTerrainComponent::extendCameraRange(Ge::Camera* camera)
{
    const auto existing = std::find_if(m_cameraRanges.begin(), m_cameraRanges.end(),
        [camera](const CameraRange& range) { return range.camera == camera; });
    if (existing != m_cameraRanges.end()) return;

    // Selection uses chunk boxes: a selected distant box can extend beyond
    // viewDistance by one root chunk. Save the user's projection for Stop.
    const auto& config = m_generator->getConfig();
    const float requiredFar = config.viewDistance
        + std::ldexp(64.0f * config.cellSize, static_cast<int>(config.maxLod));
    const float previousFar = camera->getFar();
    if (requiredFar <= previousFar) return;
    m_cameraRanges.push_back({camera, previousFar, requiredFar});
    camera->setFar(requiredFar);
}

void ProceduralTerrainComponent::update()
{
    if (!m_generator || !m_pc) return;
    Ge::Camera* camera = m_pc->cameraManager->getCurrentCamera();
    if (camera == nullptr) return;
    extendCameraRange(camera);
    m_generator->update(camera->getPosition());
}

void ProceduralTerrainComponent::stop()
{
    // Unregister before releasing geometry/programs. shutdown() is idempotent,
    // so an explicit Stop followed by component destruction is safe.
    m_generator.reset();
    if (!m_cameraRanges.empty())
    {
        const auto liveObjects = Ge::GObject::GetGObjects();
        for (const CameraRange& range : m_cameraRanges)
        {
            // A camera can be deleted or replaced while the scene is running.
            const auto found = std::find(liveObjects.begin(), liveObjects.end(), range.camera);
            if (found != liveObjects.end())
            {
                auto* camera = dynamic_cast<Ge::Camera*>(*found);
                // Preserve an explicit camera edit made by another component/user.
                if (camera != nullptr && camera->getFar() == range.appliedFar)
                    camera->setFar(range.originalFar);
            }
        }
    }
    m_cameraRanges.clear();
    m_pc = nullptr;
}

void ProceduralTerrainComponent::onGUI()
{
    ImGui::TextUnformatted("Procedural terrain");
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
    if (!m_generator) return;
    const auto& stats = m_generator->getStats();
    ImGui::Text("Chunks: %u visible / %u selected / %u resident",
        stats.visibleChunks, stats.selectedChunks, stats.residentChunks);
    ImGui::Text("Queue: %u / GPU jobs: %u", stats.queuedChunks, stats.pendingJobs);
    ImGui::Text("Collisions: %u chunks / %u pending / %llu failed", stats.collisionChunks,
        stats.pendingCollisionJobs, static_cast<unsigned long long>(stats.failedCollisionJobs));
    ImGui::Text("Mesh memory: %.1f / %.1f MiB", stats.allocatedBytes / 1048576.0,
        stats.budgetBytes / 1048576.0);
    ImGui::Text("Last generation: %.3f ms", stats.lastGenerationMilliseconds);
    if (stats.rebuilding && stats.visibleChunks == 0)
        ImGui::TextWrapped("Preparing the initial terrain coverage. Rendering starts when all selected chunks are ready.");
    if (stats.resourceLimited) ImGui::TextWrapped("Terrain memory or chunk budget reached.");
    if (!m_generator->getLastError().empty())
        ImGui::TextWrapped("%s", m_generator->getLastError().c_str());
}

std::string ProceduralTerrainComponent::serialize()
{
    return JS::serializeStruct(*this);
}

void ProceduralTerrainComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if (context.parseTo(*this) != JS::Error::NoError)
        Ge::Debug::Warn("Invalid ProceduralTerrainComponent parameters; check scene script JSON.");
}
