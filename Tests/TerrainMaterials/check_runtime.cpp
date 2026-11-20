// Exercise production terrain lifecycle/setters/draw state. Scheduling, data
// validation and the GL call sites are real; only engine services/GL are doubles.
#include "glcore.hpp"
#define main unusedTerrainSchedulerMain
#include "../Terrain/check_scheduler.cpp"
#undef main
#include <limits>

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
            "atomic invalid-data rejection, UBO range restoration, gamma updates, restart/failure cleanup\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
