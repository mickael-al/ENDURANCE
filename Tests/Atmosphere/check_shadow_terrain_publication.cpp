// Reuse only the existing scheduler's engine/GPU doubles and helpers. The
// production ProceduralGeneration, planner and publication code are unchanged.
#define main unusedTerrainSchedulerMain
#include "../Terrain/check_scheduler.cpp"
#undef main
#include "ShadowCasterBounds.hpp"

int main()
{
    try
    {
        using namespace Test;
        Ge::GraphiquePipelineManager pipelines;
        Ge::CameraManager cameras;
        Ge::LightManager lights;
        auto* engine = Ge::Engine::getPtrClassAddr();
        engine->graphiquePipelineManager = &pipelines;
        engine->cameraManager = &cameras;
        engine->lightManager = &lights;
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
        ProceduralGeneration terrain(scene, 1337);
        require(terrain.initialize(config), terrain.getLastError());
        Ge::ShadowCasterBoundsCache cache;
        std::vector<CustomRenderer*> renderers{&terrain};
        require(cache.update(renderers) && cache.bounds().empty(), "New terrain retained ungenerated caster bounds");
        auto revision = terrain.shadowCasterBoundsRevision();
        auto publications = terrain.getStats().publishedLodGroups;
        unsigned changes = 0;
        bool finished = false;
        for (unsigned tick = 0; tick < 4000; ++tick)
        {
            step(terrain, camera);
            const bool publishedNow = terrain.getStats().publishedLodGroups != publications;
            const bool changed = cache.update(renderers);
            require(changed == publishedNow, "Bounds changed outside a committed terrain publication");
            require((terrain.shadowCasterBoundsRevision() != revision) == publishedNow,
                "Terrain bounds token did not follow actual publication");
            const auto chunks = published(terrain);
            require(cache.bounds().size() == chunks.size(), "Published nonempty chunk missing from caster bounds");
            for (std::size_t i = 0; i < chunks.size(); ++i)
            {
                const auto& chunk = chunks[i];
                const double size = static_cast<double>(std::uint64_t(64) << chunk.lod) * config.cellSize;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const double origin = static_cast<double>(chunk.origin[axis]) * config.cellSize;
                    require(cache.bounds()[i].minimum[axis] == origin && cache.bounds()[i].maximum[axis] == origin + size,
                        "Caster bounds do not enclose the actual published chunk domain");
                }
            }
            changes += changed;
            publications = terrain.getStats().publishedLodGroups;
            revision = terrain.shadowCasterBoundsRevision();
            if (!terrain.getStats().rebuilding && !terrain.getStats().pendingJobs) { finished = true; break; }
        }
        require(finished && changes > 1 && !cache.bounds().empty(), "Stationary terrain fixture did not publish incremental coverage");
        for (unsigned tick = 0; tick < 32; ++tick)
        {
            step(terrain, camera);
            require(!cache.update(renderers) && terrain.shadowCasterBoundsRevision() == revision,
                "Converged stationary terrain invalidated caster fitting");
        }
        require(terrain.initialize(config), terrain.getLastError());
        require(terrain.shadowCasterBoundsRevision() != revision && cache.update(renderers) && cache.bounds().empty(),
            "Terrain restart reused old loaded caster bounds");
        terrain.shutdown();
        require(terrain.shadowCasterBoundsRevision() == 0 && cache.update(renderers) && cache.bounds().empty(),
            "Stopped terrain kept a bounds provider alive");
        std::cout << "PASS actual terrain publication: " << changes << " stationary geometry commits, exact full-chunk bounds, idle reuse, restart and shutdown invalidation\n";
    }
    catch (const std::exception& error) { std::cerr << "FAIL terrain caster bounds: " << error.what() << '\n'; return 1; }
}
