#include "ProceduralTerrain.hpp"
#define CLEAR_REGISTRY
#include "ProceduralGeneration.hpp"
#include "EnduranceTerrainMaterials.hpp"
#include "EnduranceTerrainGrain.hpp"
#include "EnduranceSea.hpp"
#include "EnduranceObjectPlacement.hpp"
#include "EnduranceTreeRenderer.hpp"
#include "EnduranceTerrainMaterialMigration.hpp"
#include "EnduranceTerrainMaterialPanel.hpp"
#include "EnduranceTerrainMaterialSerialization.hpp"
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
    heightAmplitude, heightFrequency, bandOffset, bandOffsetVariation, detailScale,
    seaLevel, seaBlendWidth, biomeOverride,
    microDetailStrength, microDetailScale, appearance, terrainAppearanceRevision,
    debugLod, collisionEnabled, collisionRadius, objectPlacementEnabled, objectPlacementFile,
    treeRenderingEnabled, treeRenderDistance, treeLod0Coverage, treeImpostorCoverage,
    treeLodTransition, treeModelBudgetMiB, treeFoliageTransmission,
    treeWindEnabled, treeWindStrength, treeWindSpeedX, treeWindSpeedZ, treeWindScale, treeWindDistance);

ProceduralTerrainComponent::ProceduralTerrainComponent()
    : appearance(Endurance::makeTerrainAppearance()),
      terrainAppearanceRevision(Endurance::TerrainAppearanceRevision) {}

ProceduralTerrainComponent::~ProceduralTerrainComponent()
{
    stop();
}

void ProceduralTerrainComponent::start()
{
    stop();
    m_error.clear();
    m_materialError.clear();
    m_seaError.clear();
    m_objectError.clear();
    m_treeError.clear();
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
    config.field.bandOffset = bandOffset;
    config.field.bandOffsetVariation = bandOffsetVariation;
    config.field.detailScale = detailScale;
    config.field.seaLevel = seaLevel;
    config.field.seaBlendWidth = seaBlendWidth;
    config.field.biomeOverride = biomeOverride;
    config.microDetailStrength = microDetailStrength;
    config.microDetailScale = microDetailScale;
    config.appearance = appearance;
    config.debugLod = debugLod;
    config.collisionEnabled = collisionEnabled;
    config.collisionRadius = collisionRadius;
    if(objectPlacementEnabled && !Endurance::loadObjectPlacement(objectPlacementFile,config.objects,m_objectError))
        config.objects.enabled=false; // Catalogue failure must not disable terrain.
    try
    {
        config.grainImage = Endurance::terrainGrainImage();
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
    else if (m_pc->renderingEngine)
    {
        // Water art belongs to the game. The terrain exposes only the tiles of
        // its last complete publication, including empty terrain chunks.
        try
        {
            m_sea = std::make_unique<Endurance::SeaRenderer>();
            if (!m_sea->initialize(*m_pc))
            {
                m_seaError = m_sea->error();
                m_sea.reset();
            }
            else
                m_pc->renderingEngine->addTransparencyCallback(this,
                    [this](bool* depthWrite)
                    {
                        if (!m_generator || !m_sea || !m_showSea) return;
                        const auto& settings = m_generator->getConfig();
                        if (m_sea->setTiles(m_generator->getSeaTiles(),
                            m_generator->getCoverageRevision(), settings.field.seaLevel,
                            64.0f * settings.cellSize))
                        {
                            m_seaError.clear();
                            m_sea->draw(depthWrite);
                        }
                        else m_seaError = m_sea->error();
                    }, Ge::RenderingEngine::TransparencyPhase::BeforeMaterials);
        }
        catch (const std::exception& error)
        {
            m_seaError = error.what();
            m_sea.reset();
        }
    }
    if(m_generator && m_generator->getConfig().objects.enabled)
    {
        try
        {
            EnduranceTrees::RenderSettings settings;
            settings.maximumDistance=treeRenderDistance; settings.lod0Coverage=treeLod0Coverage;
            settings.impostorCoverage=treeImpostorCoverage; settings.transitionFraction=treeLodTransition;
            settings.modelBudgetMiB=treeModelBudgetMiB;
            settings.foliageTransmission=treeFoliageTransmission;
            settings.windEnabled=treeWindEnabled; settings.windStrength=treeWindStrength;
            settings.windSpeed={treeWindSpeedX,treeWindSpeedZ}; settings.windScale=treeWindScale; settings.windDistance=treeWindDistance;
            m_trees=std::make_unique<Endurance::TreeRenderer>();
            if(!m_trees->initialize(*m_pc,*m_generator,settings))
            { m_treeError=m_trees->error(); m_trees.reset(); }
            else m_trees->setEnabled(treeRenderingEnabled);
        }
        catch(const std::exception& error) { m_treeError=error.what(); m_trees.reset(); }
    }
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
    if (m_pc && m_pc->renderingEngine)
        m_pc->renderingEngine->removeTransparencyCallback(this);
    m_sea.reset();
    m_trees.reset(); // Uses the generator's published GPU buffer; detach first.
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
    ImGui::Checkbox("Afficher les LOD (shader debug)", &debugLod);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Change de variante de shader sans regenerer le terrain.\n"
            "La premiere activation peut compiler le programme ; le rendu normal ne contient pas ce debug.");
    if (m_generator && debugLod != m_generator->getConfig().debugLod)
    {
        if (!m_generator->setTerrainDebugLod(debugLod))
        {
            debugLod = m_generator->getConfig().debugLod;
            m_materialError = m_generator->getLastError();
        }
        else m_materialError.clear();
    }
    if (Endurance::drawTerrainMaterialEditor(appearance, microDetailStrength, microDetailScale, m_materialBiome))
    {
        m_materialError.clear();
        if (m_generator && !m_generator->setTerrainAppearance(appearance, microDetailStrength, microDetailScale))
            m_materialError = m_generator->getLastError();
    }
    if (!m_materialError.empty()) ImGui::TextWrapped("%s", m_materialError.c_str());
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
    if(ImGui::CollapsingHeader("Points d'apparition des objets"))
    {
        ImGui::Checkbox("Generer les points (apres regeneration)", &objectPlacementEnabled);
        ImGui::TextWrapped("Positions GPU stables partagees par tous les LOD des arbres.");
        ImGui::TextWrapped("Catalogue : %s", objectPlacementFile.c_str());
        if(!m_objectError.empty()) ImGui::TextWrapped("%s",m_objectError.c_str());
        if(m_generator)
        {
            const auto objects=m_generator->getObjectPlacementStats();
            ImGui::Text("%u points publies / %u blocs / %u secteurs",objects.instances,objects.blocks,objects.sectors);
            ImGui::Text("Pools %.2f Mio / temporaire %.2f Mio",objects.poolBytes/1048576.0,objects.scratchBytes/1048576.0);
            ImGui::Text("Collectes %llu / reutilisations %llu",static_cast<unsigned long long>(objects.captureJobs),
                static_cast<unsigned long long>(objects.cachedJobs));
            ImGui::Text("Budgets atteints : pool %llu / secteurs %llu",static_cast<unsigned long long>(objects.capacityFailures),
                static_cast<unsigned long long>(objects.sectorFailures));
            if(!m_generator->getObjectPlacementError().empty())
                ImGui::TextWrapped("%s",m_generator->getObjectPlacementError().c_str());
        }
    }
    if(ImGui::CollapsingHeader("Affichage des arbres / LOD"))
    {
        if(ImGui::Checkbox("Afficher les arbres",&treeRenderingEnabled) && m_trees)
            m_trees->setEnabled(treeRenderingEnabled);
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Effet immediat, sans regenerer le terrain. Suspend le rendu et la preparation des arbres.\n"
                "Les modeles deja charges restent en cache pour comparer les performances sans rechargement.");
        ImGui::DragFloat("Distance maximale (m)",&treeRenderDistance,16.0f,64.0f,16384.0f,"%.0f");
        ImGui::SliderFloat("LOD0 : taille projetee",&treeLod0Coverage,0.15f,0.8f,"%.3f");
        ImGui::SliderFloat("LOD3 : taille projetee",&treeImpostorCoverage,0.005f,0.09f,"%.3f");
        ImGui::SliderFloat("Largeur des transitions",&treeLodTransition,0.0f,0.2f,"%.2f");
        int budget=static_cast<int>(treeModelBudgetMiB);
        if(ImGui::SliderInt("Cache modeles (Mio)",&budget,64,2048)) treeModelBudgetMiB=static_cast<std::uint32_t>(budget);
        ImGui::SliderFloat("Transmission du feuillage",&treeFoliageTransmission,0.0f,1.0f,"%.2f");
        ImGui::Checkbox("Vent sur les arbres",&treeWindEnabled);
        ImGui::SliderFloat("Vent : amplitude / hauteur",&treeWindStrength,0.0f,0.05f,"%.3f");
        ImGui::DragFloat("Vent : vitesse X (m/s)",&treeWindSpeedX,0.1f,-100.0f,100.0f,"%.1f");
        ImGui::DragFloat("Vent : vitesse Z (m/s)",&treeWindSpeedZ,0.1f,-100.0f,100.0f,"%.1f");
        ImGui::DragFloat("Vent : taille des rafales (m)",&treeWindScale,1.0f,1.0f,2048.0f,"%.0f");
        ImGui::DragFloat("Vent : distance maximale (m)",&treeWindDistance,8.0f,16.0f,4096.0f,"%.0f");
        ImGui::TextWrapped("Regenerer pour appliquer ces reglages. Le vent se desactive progressivement avec la distance et avant les imposteurs. "
            "Les feuilles utilisent une diffusion douce et une transmission de lumiere ; le bois reste opaque. LOD0 detaille, LOD1 simplifie, LOD3 imposteur 17 vues. "
            "Les seuils sont des fractions de la hauteur de l'ecran, adaptees a la taille de chaque arbre. "
            "La distance ne cree pas de nouveaux points au-dela des secteurs deja generes.");
        if(!m_treeError.empty()) ImGui::TextWrapped("%s",m_treeError.c_str());
        if(m_trees)
        {
            const auto& s=m_trees->stats();
            ImGui::Text("Modeles : %u charges / %u en chargement / %u en erreur",s.loadedModels,s.pendingModels,s.failedModels);
            ImGui::Text("Limite memoire : %u modeles reportes",s.budgetSkippedModels);
            ImGui::Text("GPU : modeles %.2f Mio / instances et culling %.2f Mio",s.modelBytes/1048576.0,s.workingBytes/1048576.0);
            ImGui::Text("%u instances / %u blocs camera / %u blocs ombres",s.instances,s.cameraBlocks,s.shadowBlocks);
            ImGui::Text("Cache transformations : %llu pages nouvelles / %llu reutilisees",
                static_cast<unsigned long long>(s.transformPagesBuilt),static_cast<unsigned long long>(s.transformPagesReused));
            ImGui::Text("Chargement CPU cumule %.1f ms / appels d'upload GL %.1f ms",
                s.assetLoadMilliseconds,s.assetUploadMilliseconds);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Le chargement travaille en arriere-plan. Ces temps CPU cumules\n"
                    "ne sont ni une duree totale de generation, ni des temps GPU de rendu.");
            if(!m_trees->error().empty()) ImGui::TextWrapped("%s",m_trees->error().c_str());
        }
    }
    if (!m_seaError.empty()) ImGui::TextWrapped("Mer : %s", m_seaError.c_str());
    if (m_generator && m_sea)
    {
        ImGui::Checkbox("Afficher la mer", &m_showSea);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Active/desactive uniquement le rendu de l'eau, sans regenerer le terrain.\n"
                "Permet de comparer son cout d'affichage pendant la generation des chunks.");
        ImGui::Text("Mer : %u tuiles", static_cast<unsigned>(m_generator->getSeaTiles().size()));
    }
    if (m_pc && m_pc->renderingEngine)
    {
        auto& rendering = *m_pc->renderingEngine;
        bool timings = rendering.isFrameTimingsEnabled();
        if (ImGui::Checkbox("Mesurer les saccades", &timings))
            rendering.setFrameTimingsEnabled(timings);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mesure le CPU et une chronologie GPU recueillie sans attente bloquante.\n"
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
            const auto& profiler = rendering.getGpuFrameProfiler();
            const auto& gpu = profiler.last();
            const auto& gpuPeak = profiler.peak();
            if (gpu.sampleNumber)
            {
                ImGui::Text("Chronologie GPU : %.3f ms / pic %.3f ms", gpu.totalMilliseconds, gpuPeak.totalMilliseconds);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Resultats differes, sans HUD ni presentation.\n"
                        "Les intervalles peuvent inclure les pauses du GPU en attente de commandes CPU.\n"
                        "Les valeurs suivantes appartiennent a la derniere image GPU mesuree.");
                static const char* labels[] = {"Preparation", "Opaque", "Ombres", "Atmosphere", "Eclairage", "Transparence", "Post-traitement"};
                for (unsigned i = 0; i < Ge::FrameGpuProfiler::StageCount; ++i)
                    ImGui::Text("  %s : %.3f ms", labels[i], gpu.milliseconds[i]);
            }
            else if (timings) ImGui::TextUnformatted("Chronologie GPU en attente de resultats...");
            if (profiler.droppedSamples())
                ImGui::Text("Echantillons GPU ignores (file pleine) : %llu",
                    static_cast<unsigned long long>(profiler.droppedSamples()));
        }
    }
    if (!m_generator) return;
    const auto& stats = m_generator->getStats();
    ImGui::Text("Coverage: %u meshes / %u selected / %u resident",
        stats.visibleChunks, stats.selectedChunks, stats.residentChunks);
    ImGui::Text("Camera: %u drawn / %u culled", stats.cameraDrawChunks, stats.cameraCulledChunks);
    ImGui::Text("Shadow passes: %u drawn / %u culled", stats.shadowDrawChunks, stats.shadowCulledChunks);
    ImGui::Text("Queue (current group): %u / GPU jobs: %u / LOD groups: %u",
        stats.queuedChunks, stats.pendingJobs, stats.pendingLodGroups);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Queue counts unsent chunks in the current group, not all remaining terrain.\n"
            "GPU jobs counts submitted chunks awaiting completion, not hardware GPU occupancy.");
    ImGui::Text("CPU plan: %s / next group: %s",
        stats.cpuPlanning ? "working" : "idle",
        stats.lookaheadReady ? "ready" : "not ready");
    ImGui::Text("Planning waits: %llu frames / immediate handoffs: %llu",
        static_cast<unsigned long long>(stats.planningWaitFrames),
        static_cast<unsigned long long>(stats.sameFrameHandoffs));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Planning waits count updates with no terrain GPU job while a CPU plan is pending.\n"
            "Immediate handoffs submit the next group in the update that publishes the previous one.\n"
            "These are scheduling counters, not GPU timing measurements.");
    if (stats.plannerWaiting)
        ImGui::TextUnformatted("Terrain generation is waiting for CPU planning.");
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
    const char* coverageStatus=stats.coverageComplete?"complete":stats.generationBlocked?"blocked / incomplete":
        stats.rebuilding?"running":"waiting / incomplete";
    ImGui::Text("Coverage generation: %.3f s (%s)",stats.coverageMilliseconds/1000.0,coverageStatus);
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Elapsed terrain streaming time, including frame scheduling and CPU waits.\n"
            "Initial shader compilation and tree asset loading are separate.\n"
            "Only 'complete' means the requested terrain coverage has converged.");
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
    if(stats.coverageComplete)
        ImGui::TextUnformatted("Requested terrain coverage is complete.");
    else if(stats.generationBlocked)
        ImGui::TextUnformatted("Terrain generation stopped before completion; check budgets and errors.");
    else if (stats.rebuilding || stats.pendingJobs)
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
    // ParseContext skips removed properties by default, including the old
    // climateFrequency setting; older scenes use the fixed shader recipe.
    const auto previousAppearance = appearance;
    const auto previousRevision = terrainAppearanceRevision;
    // Seed absent fields with this biome's current recipe. Saved artist edits
    // overwrite them; pre-material scenes simply keep the current defaults.
    appearance = Endurance::makeTerrainAppearance();
    terrainAppearanceRevision = 0;
    JS::ParseContext context(data);
    const bool parsed = context.parseTo(*this) == JS::Error::NoError;
    if (!parsed)
        Ge::Debug::Warn("Invalid ProceduralTerrainComponent parameters; check scene script JSON.");
    if (!parsed || !ProceduralTerrain::validTerrainAppearance(appearance))
    {
        appearance = previousAppearance;
        terrainAppearanceRevision = previousRevision;
        Ge::Debug::Warn("Invalid terrain appearance; retaining the previous valid palettes.");
    }
    else
    {
        Endurance::upgradeTerrainAppearance(appearance, terrainAppearanceRevision);
    }
}
