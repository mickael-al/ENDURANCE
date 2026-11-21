// Exercise production terrain lifecycle/setters/draw state. Scheduling, data
// validation and the GL call sites are real; only engine services/GL are doubles.
#include "glcore.hpp"
#define main unusedTerrainSchedulerMain
#include "../Terrain/check_scheduler.cpp"
#undef main
#include <limits>
#include "ProceduralTerrainBiomes.hpp"

int main()
{
    try
    {
        using namespace Test;
        using namespace TerrainFixtureGL;
        Ge::GraphiquePipelineManager pipelines;
        Ge::CameraManager cameras;
        Ge::LightManager lights;
        Ge::SettingManager settings;
        auto* engine = Ge::Engine::getPtrClassAddr();
        engine->graphiquePipelineManager = &pipelines;
        engine->cameraManager = &cameras;
        engine->lightManager = &lights;
        engine->settingManager = &settings;
        auto& camera = cameras.camera;
        camera.setPosition({1, 18, 3});
        SceneData scene;
        ProceduralGeneration::Config config;
        config.field.mode = FieldMode::Plane;
        config.maxLod = 2;
        config.viewDistance = 192;
        config.maxSelectedChunks = 256;
        config.maxResidentChunks = 512;
        config.maxChunksPerFrame = 2;
        config.maxInFlight = 4;
        config.collisionEnabled = false;

        // A custom draw borrows both a nonzero UBO range and an independently
        // selected generic binding, as happens between atmosphere/material work.
        glBindBufferRange(GL_UNIFORM_BUFFER, 6, 900, 256, 512);
        glBindBuffer(GL_UNIFORM_BUFFER, 901);
        ProceduralGeneration terrain(scene, 1337);
        require(terrain.initialize(config), terrain.getLastError());
        require(uniform == 901 && uniformBindings[6].buffer == 900,
            "DSA material initialization changed enclosing UBO state");
        require(pipelines.registeredRenderers == 1 && pipelines.activePipelines == 2,
            "Terrain initialization leaked or duplicated renderer registration");
        require(buffers.size() == 1 && buffers.begin()->second.size() == sizeof(TerrainMaterialTable),
            "Material buffer did not match the complete fixed table");
        require(terrain.getStats().materialUploads == 1,
            "Initial table was not uploaded exactly once");
        const auto firstUploads = uploads;
        const auto firstUniformWrites = uniformWrites;
        require(terrain.setTerrainAppearance(config.appearance, config.microDetailStrength, config.microDetailScale),
            terrain.getLastError());
        require(uploads == firstUploads && uniformWrites == firstUniformWrites,
            "Unchanged material settings caused a GPU upload");

        settle(terrain, camera);
        require(terrain.getStats().cameraDrawChunks > 0, "Runtime fixture did not exercise terrain draw");
        require(uniform == 901 && uniformBindings[6].buffer == 900
            && uniformBindings[6].start == 256 && uniformBindings[6].size == 512,
            "Terrain draw did not restore UBO range and generic binding");
        const auto geometry = identities(published(terrain));
        const auto shadowRevision = terrain.shadowContentRevision();
        const auto oldSubmissions = submissions;
        const auto oldPublications = terrain.getStats().publishedLodGroups;

        auto edited = config.appearance;
        edited.biomes[0].ground.base.colorLow[0] = 0.11f;
        const float nan = (std::numeric_limits<float>::quiet_NaN)();
        require(!terrain.setTerrainAppearance(edited, nan, 2.0f),
            "Invalid detail value partly applied a valid palette transaction");
        require(uploads == firstUploads
            && terrain.getConfig().appearance.biomes[0].ground.base.colorLow[0]
                == config.appearance.biomes[0].ground.base.colorLow[0],
            "Rejected transaction changed the table or GPU store");
        auto invalid = edited;
        invalid.biomes[1].wall.accent.response[1] = nan;
        require(!terrain.setTerrainAppearance(invalid, 0.25f, 3.0f)
            && terrain.getConfig().microDetailStrength == config.microDetailStrength,
            "Invalid material partly applied valid microdetail controls");

        require(terrain.setTerrainAppearance(edited, 0.25f, 3.0f), terrain.getLastError());
        require(terrain.getLastError().empty(), "A corrected material transaction retained its validation error");
        require(uploads == firstUploads + 1 && terrain.getStats().materialUploads == 2
            && terrain.getStats().materialUploadBytes == 2 * sizeof(TerrainMaterialTable),
            "Live table edit did not produce exactly one bounded upload");
        require(std::memcmp(buffers.begin()->second.data(), edited.biomes.data(), sizeof(TerrainMaterialTable)) == 0,
            "Uploaded material bytes differ from accepted parameters");
        edited.macroPeriod = 512.0f;
        edited.patchPeriod = 48.0f;
        edited.debugView = 3;
        require(terrain.setTerrainAppearance(edited), terrain.getLastError());
        require(uploads == firstUploads + 1 && floatUniforms[locations.at("terrainMacroPeriod")] == 512.0f,
            "A global control edit reuploaded recipes or missed its uniform");
        for (unsigned tick = 0; tick < 8; ++tick) step(terrain, camera);
        require(submissions == oldSubmissions && terrain.getStats().publishedLodGroups == oldPublications
            && identities(published(terrain)) == geometry && terrain.shadowContentRevision() == shadowRevision,
            "Appearance changes restarted generation or invalidated geometry/shadow coverage");

        const auto beforeCachedSwitch = pipelines.createdPipelines;
        edited.debugView = 0;
        edited.macroPeriod = 768.0f;
        require(terrain.setTerrainAppearance(edited), terrain.getLastError());
        terrain.drawRenderer();
        require(pipelines.createdPipelines == beforeCachedSwitch
            && programFloatUniforms[{lastDrawProgram,locations.at("terrainMacroPeriod")}] == 768.0f
            && programFloatUniforms[{lastDrawProgram,locations.at("terrainPatchPeriod")}] == 48.0f
            && programFloatUniforms[{lastDrawProgram,locations.at("microDetailStrength")}] == 0.25f
            && programFloatUniforms[{lastDrawProgram,locations.at("microDetailScale")}] == 3.0f,
            "Cached normal variant was recompiled or retained stale numeric controls");
        const auto normalProgram = lastDrawProgram;
        const auto beforeRejectedUpload = uploads;
        const auto beforeRejectedPipelines = pipelines.activePipelines;
        auto rejected = edited;
        rejected.debugView = 6;
        rejected.biomes[0].ground.base.colorLow[0] = 0.73f;
        pipelines.failNextPipeline = true;
        require(!terrain.setTerrainAppearance(rejected,0.0f,6.0f), "Failed shader variant was accepted");
        terrain.drawRenderer();
        require(lastDrawProgram == normalProgram && uploads == beforeRejectedUpload
            && pipelines.activePipelines == beforeRejectedPipelines
            && terrain.getConfig().appearance.debugView == 0
            && terrain.getConfig().microDetailStrength == 0.25f
            && terrain.getConfig().appearance.biomes[0].ground.base.colorLow[0] == edited.biomes[0].ground.base.colorLow[0],
            "Shader failure partly changed the visible program, palette or detail controls");
        blockBytes = 16;
        require(!terrain.setTerrainAppearance(rejected,0.0f,6.0f), "Incompatible shader ABI variant was accepted");
        blockBytes = sizeof(TerrainMaterialTable);
        terrain.drawRenderer();
        require(lastDrawProgram == normalProgram && uploads == beforeRejectedUpload
            && pipelines.activePipelines == beforeRejectedPipelines,
            "Failed shader ABI check evicted the valid program, leaked or partly uploaded its palette");
        require(terrain.setTerrainAppearance(edited), terrain.getLastError());
        require(terrain.getLastError().empty(), "Successful edit retained a previous variant compilation error");
        require(terrain.setTerrainDebugLod(true), terrain.getLastError());
        require(pipelines.createdDefines.back().find("#define DEBUG_TERRAIN_LOD 1") != std::string::npos,
            "LOD inspection was not compiled as a shader variant");
        const auto beforeLodUnchanged = pipelines.createdPipelines;
        require(terrain.setTerrainDebugLod(true) && pipelines.createdPipelines == beforeLodUnchanged,
            "Unchanged LOD inspection caused recompilation");
        require(terrain.setTerrainDebugLod(false), terrain.getLastError());
        terrain.drawRenderer();
        require(lastDrawProgram == normalProgram, "Leaving LOD debug did not reuse production program");
        for (unsigned detail=0; detail<2; ++detail) for (int debug=0; debug<7; ++debug)
        {
            edited.debugView = debug;
            require(terrain.setTerrainAppearance(edited,detail ? 0.25f : 0.0f,3.0f),terrain.getLastError());
            require(pipelines.activePipelines <= 9, "Unbounded terrain variant cache");
            terrain.drawRenderer();
            require(programUIntUniforms[{lastDrawProgram,locations.at("worldSeed")}] == 1337u
                && programUIntArrays[{lastDrawProgram,locations.at("terrainBiomeMatrix[0]")}]
                    == std::vector<unsigned>(BiomeMatrix2D.begin(),BiomeMatrix2D.end())
                && programFloatUniforms[{lastDrawProgram,locations.at("terrainSeaLevel")}] == config.field.seaLevel
                && programFloatUniforms[{lastDrawProgram,locations.at("terrainSeaBlendWidth")}] == config.field.seaBlendWidth
                && programFloatUniforms[{lastDrawProgram,locations.at("terrainAlbedoGamma")}] == settings.gamma,
                "A terrain variant missed immutable fields, biome matrix or gamma initialization");
        }
        edited.debugView = 0;
        require(terrain.setTerrainAppearance(edited,0.25f,3.0f),terrain.getLastError());
        require(submissions == oldSubmissions && terrain.shadowContentRevision() == shadowRevision,
            "Render variant changes touched terrain or shadow geometry");
        // Per-layer visibility queries reuse the draw's existing frustum cache.
        const auto culls = terrain.getStats().shadowCullUpdates;
        const auto nearToken = terrain.shadowLayerContentRevision(0,glm::mat4(1));
        require(nearToken != 0 && terrain.getStats().shadowCullUpdates == culls,
            "Layer token query repeated the draw's unchanged culling work");
        require(terrain.shadowLayerContentRevision(0,glm::mat4(1)) == nearToken,
            "Unchanged layer returned a new token");
        glm::mat4 outside(1.0f); outside[3][0]=1000000.0f;
        const auto emptyToken = terrain.shadowLayerContentRevision(0,outside);
        require(emptyToken != nearToken && terrain.shadowLayerContentRevision(0,outside) == emptyToken,
            "Empty shadow selection did not invalidate exactly once");
        require(terrain.shadowLayerContentRevision(0,glm::mat4(1)) != emptyToken,
            "Returning casters reused an empty-map token");

        const auto unchangedWrites = uniformWrites;
        terrain.drawRenderer();
        require(uniformWrites == unchangedWrites, "Steady gamma was reuploaded every draw");
        settings.gamma = 1.8f;
        terrain.drawRenderer();
        require(uniformWrites == unchangedWrites + 1
            && floatUniforms[locations.at("terrainAlbedoGamma")] == 1.8f,
            "Live engine gamma change did not refresh the G-buffer adapter");
        settings.gamma = nan;
        terrain.drawRenderer();
        require(floatUniforms[locations.at("terrainAlbedoGamma")] == 2.2f,
            "Nonfinite gamma escaped the material uniform guard");
        glBindBufferBase(GL_UNIFORM_BUFFER, 6, 0);
        glBindBuffer(GL_UNIFORM_BUFFER, 902);
        terrain.drawRenderer();
        require(uniformBindings[6].buffer == 0 && uniform == 902,
            "Initially unbound UBO slot was not restored");

        require(terrain.initialize(config), terrain.getLastError());
        require(buffers.size() == 1 && pipelines.activePipelines == 2 && pipelines.registeredRenderers == 1,
            "Terrain restart leaked material storage or registration");
        terrain.shutdown();
        require(buffers.empty() && pipelines.activePipelines == 0 && pipelines.registeredRenderers == 0,
            "Terrain shutdown retained GPU storage or renderer registration");
        allocationFailure = true;
        require(!terrain.initialize(config), "Material allocation failure was accepted");
        allocationFailure = false;
        require(buffers.empty() && pipelines.activePipelines == 0 && pipelines.registeredRenderers == 0,
            "Failed initialization leaked GPU storage or registration");
        blockBytes = 16;
        require(!terrain.initialize(config), "Incorrect shader material ABI was accepted");
        blockBytes = sizeof(ProceduralTerrain::TerrainMaterialTable);
        require(buffers.empty() && pipelines.activePipelines == 0 && pipelines.registeredRenderers == 0,
            "Shader ABI rejection leaked GPU storage or registration");
        require(terrain.initialize(config), terrain.getLastError());
        terrain.shutdown();
        std::cout << "PASS production terrain materials: live edits without remeshing, upload deduplication, "
            "bounded compiled variants, atomic failed-build rejection, uniform restoration, UBO range restoration, gamma updates, shared shadow selection, restart/failure cleanup\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
