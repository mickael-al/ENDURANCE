#include "ProceduralTerrain.hpp"
#define CLEAR_REGISTRY
#include "ProceduralGeneration.hpp"
#include "Editor.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "Debug.hpp"
#include <algorithm>
#include <exception>

// Register in this translation unit only, so including the component header
// cannot create duplicate factories or duplicate JSON specializations.
REGISTER(Behaviour, ProceduralTerrainComponent, seed, cellSize, maxLod,
    viewDistance, lodDistanceFactor, maxChunksPerFrame, maxInFlight,
    vertexBudgetMiB, maxResidentChunks, maxSelectedChunks, baseHeight,
    heightAmplitude, heightFrequency, climateFrequency, bandThickness,
    bandVariation, bandOffset, bandOffsetVariation, detailScale,
    seaLevel, seaBlendWidth, biomeOverride,
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
    config.field.seaLevel = seaLevel;
    config.field.seaBlendWidth = seaBlendWidth;
    config.field.biomeOverride = biomeOverride;
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

void ProceduralTerrainComponent::update()
{
    // The renderer prepares streaming after the active camera moves, including
    // editor pause frames where Behaviour::update is suspended.
}

void ProceduralTerrainComponent::stop()
{
    // Unregister before releasing geometry/programs. shutdown() is idempotent,
    // so an explicit Stop followed by component destruction is safe.
    m_generator.reset();
    m_pc = nullptr;
}

void ProceduralTerrainComponent::onGUI()
{
    ImGui::TextUnformatted("Procedural terrain");
    static const char* biomeNames[] = {
        "Automatique (matrices)", "Glace polaire", "Toundra gelee", "Toundra", "Taiga",
        "Foret boreale marecageuse", "Toundra rocheuse", "Steppe froide", "Foret boreale",
        "Foret froide", "Marecage froid", "Plaine seche", "Prairie", "Foret temperee",
        "Foret temperee humide", "Marais tempere", "Desert de roche", "Savane seche",
        "Savane", "Jungle seche", "Jungle tropicale", "Desert volcanique", "Desert rouge",
        "Plateau aride", "Jungle brulante", "Mangrove tropicale", "Fond marin"
    };
    int selected = std::clamp<int>(biomeOverride, -1, 25) + 1;
    if (ImGui::Combo("Biome a tester", &selected, biomeNames, 27))
        biomeOverride = selected - 1;
    // m_pc remains available after a shader error: fixing the file must not
    // require Stop/Play just to make this button reappear. start() destroys the
    // previous generator and recompiles all terrain shaders/includes from disk.
    if (m_pc)
    {
        if (ImGui::Button("Regenerer avec ces reglages")) start();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Relit et recompile les shaders de Asset/Shader/ProceduralTerrain,\n"
                "y compris les fichiers de biomes, puis reconstruit le terrain.\n"
                "Apres une erreur GLSL, corrigez le fichier et cliquez a nouveau.");
    }
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
    if (!m_generator) return;
    const auto& stats = m_generator->getStats();
    ImGui::Text("Coverage: %u meshes / %u selected / %u resident",
        stats.visibleChunks, stats.selectedChunks, stats.residentChunks);
    ImGui::Text("Camera: %u drawn / %u culled", stats.cameraDrawChunks, stats.cameraCulledChunks);
    ImGui::Text("Shadow passes: %u drawn / %u culled", stats.shadowDrawChunks, stats.shadowCulledChunks);
    ImGui::Text("Queue: %u / GPU jobs: %u / LOD groups: %u",
        stats.queuedChunks, stats.pendingJobs, stats.pendingLodGroups);
    ImGui::Text("LOD selections: %llu / published groups: %llu",
        static_cast<unsigned long long>(stats.lodSelections),
        static_cast<unsigned long long>(stats.publishedLodGroups));
    ImGui::Text("Largest LOD transaction: %u meshes / CPU plan: %.3f ms",
        stats.largestLodTransaction, stats.lastPlanningMilliseconds);
    ImGui::Text("Cull updates: camera %llu / shadows %llu",
        static_cast<unsigned long long>(stats.cameraCullUpdates),
        static_cast<unsigned long long>(stats.shadowCullUpdates));
    ImGui::Text("Draw-list uploads: %llu (%.1f KiB)",
        static_cast<unsigned long long>(stats.drawListUploads), stats.drawListUploadBytes / 1024.0);
    ImGui::Text("Collisions: %u chunks / %u pending / %llu failed", stats.collisionChunks,
        stats.pendingCollisionJobs, static_cast<unsigned long long>(stats.failedCollisionJobs));
    ImGui::Text("Mesh memory: %.1f / %.1f MiB", stats.allocatedBytes / 1048576.0,
        stats.budgetBytes / 1048576.0);
    ImGui::Text("Last generation: %.3f ms", stats.lastGenerationMilliseconds);
    if (stats.rebuilding && stats.visibleChunks == 0)
        ImGui::TextWrapped("Preparing nearby terrain. Ready local coverage appears progressively.");
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
