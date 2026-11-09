#include "ProceduralTerrain.hpp"
#define CLEAR_REGISTRY
#include "ProceduralGeneration.hpp"
#include "Editor.hpp"
#include "Engine.hpp"
#include "RenderingEngine.hpp"
#include "PointeurClass.hpp"
#include "Debug.hpp"
#include <algorithm>
#include <exception>
#include <iostream>

// Register in this translation unit only, so including the component header
// cannot create duplicate factories or duplicate JSON specializations.
REGISTER(Behaviour, ProceduralTerrainComponent, seed, cellSize, maxLod,
    viewDistance, lodDistanceFactor, maxChunksPerFrame, maxInFlight,
    columnCacheEntries, densityCacheEntries, measureGenerationTimings,
    vertexBudgetMiB, maxResidentChunks, maxSelectedChunks, baseHeight,
    heightAmplitude, heightFrequency, climateFrequency, bandOffset, bandOffsetVariation, detailScale,
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
        std::cerr << "[Terrain initialization] " << m_error << std::endl;
        return;
    }

    ProceduralGeneration::Config config;
    config.cellSize = cellSize;
    config.maxLod = maxLod;
    config.viewDistance = viewDistance;
    config.lodDistanceFactor = lodDistanceFactor;
    config.maxChunksPerFrame = maxChunksPerFrame;
    config.maxInFlight = maxInFlight;
    config.columnCacheEntries = columnCacheEntries;
    config.densityCacheEntries = densityCacheEntries;
    config.measureGenerationTimings = measureGenerationTimings;
    config.vertexBudgetMiB = vertexBudgetMiB;
    config.maxResidentChunks = maxResidentChunks;
    config.maxSelectedChunks = maxSelectedChunks;
    config.field.baseHeight = baseHeight;
    config.field.heightAmplitude = heightAmplitude;
    config.field.heightFrequency = heightFrequency;
    config.field.climateFrequency = climateFrequency;
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
        std::cerr << "[Terrain initialization] Failed: " << m_error << std::endl;
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
    // previous generator and rereads sources/includes. Unchanged programs load
    // their disk binaries; changed or rejected entries compile from source.
    if (m_pc)
    {
        if (ImGui::Button("Regenerer avec ces reglages")) start();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Applique les reglages et reconstruit le terrain.\n"
                "Relit les shaders et les biomes : seuls les programmes modifies\n"
                "ou absents du cache doivent etre recompiles.\n"
                "Apres une erreur GLSL, corrigez le fichier et cliquez a nouveau.");
    }
    if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
    if (m_pc && m_pc->renderingEngine)
    {
        auto& rendering = *m_pc->renderingEngine;
        bool timings = rendering.isFrameTimingsEnabled();
        if (ImGui::Checkbox("Mesurer les saccades", &timings))
            rendering.setFrameTimingsEnabled(timings);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mesure les appels CPU et leurs attentes pendant drawFrame, pas le temps GPU.\n"
                "Les comportements executes avant drawFrame ne sont pas inclus.\n"
                "Desactiver fige les mesures. Remettre a zero apres le chargement pour isoler un nouveau pic.\n"
                "Ne pas interrompre le debogueur pendant la capture : la pause serait comptee dans le pic.");
        const auto& last = rendering.getLastFrameTimings();
        const auto& peak = rendering.getPeakFrameTimings();
        if (timings || peak.sampleNumber != 0)
        {
            if (ImGui::Button("Remettre les mesures a zero")) rendering.resetFrameTimings();
            if (peak.sampleNumber != 0)
            {
                ImGui::Text("DrawFrame CPU/waits: last %.3f ms / peak %.3f ms (#%llu)",
                    last.totalMilliseconds, peak.totalMilliseconds,
                    static_cast<unsigned long long>(peak.sampleNumber));
                ImGui::Text("Peak: camera/cascades %.3f / terrain preparation %.3f ms",
                    peak.cameraMilliseconds, peak.prepareMilliseconds);
                ImGui::Text("Peak: animation %.3f / render+HUD %.3f / swap %.3f ms",
                    peak.animationMilliseconds, peak.renderMilliseconds, peak.swapMilliseconds);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toutes les valeurs du pic appartiennent a la meme image.\n"
                        "La preparation inclut tous les CustomRenderer, dont le terrain.\n"
                        "Swap peut inclure l'attente VSync : ce n'est pas une mesure isolee du GPU.");
            }
            else ImGui::TextUnformatted("Waiting for a complete measured frame...");
        }
    }
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
    const auto& generation = stats.generation;
    if (!m_generator->getConfig().measureGenerationTimings)
        ImGui::TextUnformatted("GPU timing disabled; coverage and cache counters remain available.");
    ImGui::Text("Coverage generation: %.1f ms%s", stats.coverageMilliseconds,
        stats.rebuilding ? " (running)" : "");
    ImGui::Text("GPU last batch: 2D %.3f / preparation %.3f / 3D %.3f / count %.3f ms",
        generation.lastField2DMilliseconds, generation.lastFieldPreparationMilliseconds,
        generation.lastVolumeMilliseconds, generation.lastCountMilliseconds);
    ImGui::Text("GPU last emission: %.3f ms / request latency: %.3f ms",
        generation.lastEmissionMilliseconds, generation.lastRequestMilliseconds);
    const double gpuTotal = generation.field2DMilliseconds + generation.fieldPreparationMilliseconds
        + generation.volumeMilliseconds + generation.countMilliseconds + generation.emissionMilliseconds;
    ImGui::Text("GPU accumulated: %.3f ms (%llu batches / %llu emissions)", gpuTotal,
        static_cast<unsigned long long>(generation.timedBatches),
        static_cast<unsigned long long>(generation.timedEmissions));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("GPU intervals are disjoint: shared work is counted once per batch.\n"
            "Queries are read later without blocking. Coverage includes frame scheduling and CPU planning.\n"
            "Compare the same scene, camera, seed and concurrency after warming shader caches.");
    ImGui::Text("Field cache hits: 2D %llu / density %llu",
        static_cast<unsigned long long>(generation.columnCacheHits),
        static_cast<unsigned long long>(generation.densityCacheHits));
    ImGui::Text("Built fields: 2D %llu / density %llu; last batch %u jobs",
        static_cast<unsigned long long>(generation.generatedColumnTiles),
        static_cast<unsigned long long>(generation.generatedDensityFields), generation.lastFieldBatchJobs);
    ImGui::Text("GPU fields: %.2f MiB cache + %.2f MiB scratch + %.2f MiB group metadata",
        generation.cachedFieldBytes / 1048576.0, generation.transientFieldBytes / 1048576.0,
        generation.groupMetadataBytes / 1048576.0);
    ImGui::Text("Free vertex memory: largest block %.2f MiB / %u ranges",
        generation.largestFreeVertexBytes / 1048576.0, generation.freeVertexRanges);
    if (generation.droppedTimingSamples)
        ImGui::Text("GPU timing samples skipped while busy: %llu",
            static_cast<unsigned long long>(generation.droppedTimingSamples));
    if (stats.rebuilding && stats.visibleChunks == 0)
        ImGui::TextWrapped("Preparing nearby terrain. Ready local coverage appears progressively.");
    if (stats.rebuilding || stats.pendingJobs)
        ImGui::TextUnformatted("Background terrain work is still active.");
    else
        ImGui::TextUnformatted("Terrain generation is idle.");
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
