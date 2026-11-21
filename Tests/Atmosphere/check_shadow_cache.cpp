#include "glcore.hpp"
#include "LightManager.hpp"
#include "DirectionalLight.hpp"
#include "PointLight.hpp"
#include "SpotLight.hpp"
#include "CustomRenderer.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "GraphicsDataMisc.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Ge;
static void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

namespace Ge
{
    // Exercise the production manager at exactly the renderer's depth-pass
    // boundary. Drawing is counted, not emulated as a cache implementation.
    class RenderingEngine
    {
    public:
        static void initialize(LightManager& lights, GraphicsDataMisc& graphics) { lights.initialize(&graphics); }
        static void release(LightManager& lights) { lights.release(); }
        static unsigned frame(LightManager& lights, const std::vector<CustomRenderer*>& providers,
            bool untrackedCasters = false, std::uint64_t immutableModels = 0)
        {
            lights.prepareShadowCasterBounds(providers);
            lights.prepareShadowMaps(providers, untrackedCasters, immutableModels);
            require(lights.getShadowContentRevision() == 0, "Pending shadow pass exposed a completed content revision");
            unsigned draws = 0;
            for (unsigned layer = 0; layer < lights.getFrameShadowBuffer().size(); ++layer)
            {
                if (!lights.shadowMapNeedsRender(layer)) continue;
                for (auto* provider : providers) if (provider) provider->drawShadowRenderer(layer);
                ++draws;
                lights.shadowMapRendered(layer);
            }
            lights.finishShadowMaps();
            require(lights.getShadowContentRevision() != 0, "Complete shadow pass did not publish a content revision");
            require(lights.getShadowRedrawnLayers() == draws, "Actual render decisions disagree with redraw counters");
            require(draws + lights.getShadowReusedLayers() == lights.getFrameShadowBuffer().size(),
                "Render/reuse counters did not cover actual allocated layers");
            return draws;
        }
    };
}

struct Terrain final : CustomRenderer
{
    std::uint64_t revision = nextShadowCasterBoundsRevision();
    unsigned draws = 0;
    void drawRenderer() override {}
    void drawShadowRenderer(unsigned) override { ++draws; }
    std::uint64_t shadowContentRevision() const override { return revision; }
    // Content changes even when bounding geometry stays exactly the same.
    void publish() { revision = nextShadowCasterBoundsRevision(); }
};
struct Unknown final : CustomRenderer
{
    unsigned draws = 0;
    void drawRenderer() override {}
    void drawShadowRenderer(unsigned) override { ++draws; }
};

struct LayeredTerrain final : CustomRenderer
{
    std::array<std::uint64_t, 4> revisions{};
    std::array<unsigned, 4> draws{};
    mutable unsigned queries = 0;
    LayeredTerrain() { for (auto& revision : revisions) revision = nextShadowCasterBoundsRevision(); }
    void drawRenderer() override {}
    void drawShadowRenderer(unsigned layer) override { ++draws.at(layer); }
    // Global fallback is deliberately unknown: the manager must use the
    // explicit layer contract, not silently fall back to a global revision.
    std::uint64_t shadowLayerContentRevision(unsigned layer, const glm::mat4&) const override
    { ++queries; return revisions.at(layer); }
};

static void managerLifecycle()
{
    LightManager lights;
    Camera camera;
    GraphicsDataMisc graphics;
    graphics.current_camera = &camera;
    Engine::getPtrClassAddr()->lightManager = &lights;
    RenderingEngine::initialize(lights, graphics);
    auto* sun = lights.createDirectionalLight({-45, 20, 0}, {1, 1, 1});
    sun->setshadow(true);
    Terrain terrain;
    std::vector<CustomRenderer*> providers{&terrain};
    require(RenderingEngine::frame(lights, providers) == 4, "Initial terrain did not render all cascades");
    const auto first = lights.getShadowContentRevision();
    for (unsigned i = 0; i < 64; ++i)
        require(RenderingEngine::frame(lights, providers) == 0 && lights.getShadowContentRevision() == first,
            "Idle terrain redrew maps or invalidated downstream volumes");
    require(terrain.draws == 4, "Idle cache still called the terrain draw path");

    terrain.publish();
    require(RenderingEngine::frame(lights, providers) == 4 && lights.getShadowContentRevision() > first,
        "Geometry publication inside identical bounds reused stale maps");
    auto version = lights.getShadowContentRevision();
    sun->setEulerAngles({-50, 20, 0});
    require(RenderingEngine::frame(lights, providers) == 4 && lights.getShadowContentRevision() > version,
        "Moving sunlight reused the previous depth projection");
    camera.position += glm::vec3(100, 0, 150);
    camera.notify(lights);
    require(RenderingEngine::frame(lights, providers) > 0, "Moving camera failed to invalidate changed cascades");

    // A regular moving/animated actor or legacy callback has no dependable
    // version. Its presence must redraw even if all terrain and matrices agree.
    for (unsigned i = 0; i < 3; ++i)
    {
        version = lights.getShadowContentRevision();
        require(RenderingEngine::frame(lights, providers, true) == 4 && lights.getShadowContentRevision() > version,
            "Untracked actor/callback froze its own or volumetric shadows");
    }
    require(RenderingEngine::frame(lights, providers) == 4, "Removing an untracked caster left its old silhouette cached");
    require(RenderingEngine::frame(lights, providers) == 0, "Known geometry did not resume reuse after removal");

    Unknown custom;
    providers.push_back(&custom);
    require(RenderingEngine::frame(lights, providers) == 4 && RenderingEngine::frame(lights, providers) == 4,
        "Unknown custom renderer silently opted into reuse");
    require(custom.draws == 8, "Unknown renderer missed a depth pass");
    providers.pop_back();
    require(RenderingEngine::frame(lights, providers) == 4 && RenderingEngine::frame(lights, providers) == 0,
        "Removing custom geometry did not clean and recache the maps");

    // A replacement allocation can retain identical layer count and matrices.
    version = lights.getShadowContentRevision();
    sun->setshadow(false);
    sun->setshadow(true);
    require(lights.getShadowContentRevision() == 0, "Texture replacement still advertised old depth content");
    require(RenderingEngine::frame(lights, providers) == 4 && lights.getShadowContentRevision() > version,
        "Same-size replacement texture reused uninitialized data");
    sun->setshadow(false);
    require(RenderingEngine::frame(lights, providers) == 0 && lights.getShadowReusedLayers() == 0,
        "Removed layers remained in the shadow cache");
    sun->setshadow(true);
    require(RenderingEngine::frame(lights, providers) == 4, "Re-enabled shadows used discarded storage");
    RenderingEngine::release(lights);
    std::cout << "PASS production LightManager: idle reuse, actual redraw counters, publication, camera/light movement, unknown actor/callback/custom caster, removal and same-size texture replacement\n";
}

static void localProjectionInvalidation()
{
    LightManager lights;
    Camera camera;
    camera.nearPlane = .05f;
    GraphicsDataMisc graphics;
    graphics.current_camera = &camera;
    Engine::getPtrClassAddr()->lightManager = &lights;
    RenderingEngine::initialize(lights, graphics);
    auto* point = lights.createPointLight({0, 3, 0}, {1, 1, 1});
    auto* spot = lights.createSpotLight({0, 3, 0}, {1, 1, 1}, {0, 0, 0}, 60);
    point->setshadow(true); spot->setshadow(true);
    camera.notify(lights);
    Terrain terrain;
    const std::vector<CustomRenderer*> providers{&terrain};
    require(RenderingEngine::frame(lights, providers) == 7, "Local light layer count disagrees with depth storage");
    auto writes = FixtureGL::writes[graphics.str_ssbo.str_shadow];
    camera.position += glm::vec3(10, 3, -4);
    camera.notify(lights);
    require(FixtureGL::writes[graphics.str_ssbo.str_shadow] == writes &&
        RenderingEngine::frame(lights, providers) == 0,
        "Camera translation rebuilt or redrew stationary local lights");
    auto previous = lights.getShadowMatrix();
    camera.nearPlane = .02f; camera.farPlane = 4096;
    camera.notify(lights);
    auto updated = lights.getShadowMatrix();
    require(updated.size() == previous.size(), "Clip change reallocated local light topology");
    for (std::size_t i = 0; i < updated.size(); ++i)
        require(std::memcmp(&updated[i], &previous[i], sizeof(glm::mat4)) != 0,
            "Camera clip change left a local light projection stale");
    require(RenderingEngine::frame(lights, providers) == 7, "Changed local depth projection reused old texture content");
    RenderingEngine::release(lights);
    std::cout << "PASS local point/spot projections: clip changes refresh depth, camera translation preserves uploads/maps\n";
}

static void individualLayers()
{
    ShadowMapCache cache;
    std::vector<glm::mat4> matrices(4, glm::mat4(1));
    int identity;
    std::vector<ShadowContentSource> sources{{&identity, 1}};
    cache.prepare(matrices, sources, false);
    cache.rendered(0); cache.rendered(1); cache.rendered(2); cache.finish();
    require(cache.contentRevision() == 0, "Incomplete cascade set was published as complete");
    cache.rendered(3); cache.finish();
    const auto initial = cache.contentRevision();
    matrices[2][3].x = .25f;
    cache.prepare(matrices, sources, false);
    require(!cache.needsRender(0) && !cache.needsRender(1) && cache.needsRender(2) && !cache.needsRender(3),
        "One changed cascade invalidated stable layers or reused the changed layer");
    cache.rendered(2); cache.finish();
    require(cache.redrawnLayers() == 1 && cache.reusedLayers() == 3 && cache.contentRevision() > initial,
        "Partial refresh counters or downstream revision wrong");
    const auto partial = cache.contentRevision();
    cache.finish();
    require(cache.contentRevision() == partial, "Completing a frame twice changed content revision");
    sources[0].revision = 2; // renderer restarted at the same address
    cache.prepare(matrices, sources, false);
    for (unsigned i = 0; i < 4; ++i) require(cache.needsRender(i), "Renderer restart reused prior lifetime content");
    matrices[0][0].x = std::numeric_limits<float>::quiet_NaN();
    for (unsigned frame = 0; frame < 2; ++frame)
    {
        cache.prepare(matrices, sources, false);
        require(cache.needsRender(0), "Nonfinite projection became cacheable under fast-math");
        for (unsigned i = 0; i < 4; ++i) cache.rendered(i);
        cache.finish();
    }
    std::cout << "PASS per-layer cache: selective refresh, atomic completed revision, restart tokens, no incomplete/NaN reuse\n";
}

static void immutableModelIntegration()
{
    LightManager lights;
    Camera camera;
    GraphicsDataMisc graphics;
    graphics.current_camera = &camera;
    Engine::getPtrClassAddr()->lightManager = &lights;
    RenderingEngine::initialize(lights, graphics);
    lights.createDirectionalLight({-45,20,0},{1,1,1})->setshadow(true);
    Terrain terrain;
    const std::vector<CustomRenderer*> providers{&terrain};
    std::uint64_t models = 17;
    require(RenderingEngine::frame(lights, providers, false, models) == 4,
        "Immutable reference model did not cast its initial shadow");
    const auto first = lights.getShadowContentRevision();
    for (unsigned i=0;i<64;++i)
        require(RenderingEngine::frame(lights, providers, false, models) == 0 &&
            lights.getShadowContentRevision() == first,
            "Stationary immutable model invalidated terrain/tree/volume shadows");
    ++models;
    require(RenderingEngine::frame(lights, providers, false, models) == 4 &&
        RenderingEngine::frame(lights, providers, false, models) == 0,
        "Immutable model movement or alpha edit failed to update and recache maps");
    terrain.publish();
    require(RenderingEngine::frame(lights, providers, false, models) == 4,
        "Immutable model token hid a new terrain publication");
    require(RenderingEngine::frame(lights, providers, true, 0) == 4 &&
        RenderingEngine::frame(lights, providers, true, 0) == 4,
        "Untracked models/callbacks inherited immutable model reuse");
    require(RenderingEngine::frame(lights, providers, false, models) == 4 &&
        RenderingEngine::frame(lights, providers, false, models) == 0,
        "Returning to known immutable inputs reused unknown previous content");
    require(RenderingEngine::frame(lights, providers, false, 0) == 4,
        "Removing immutable model source left its old shadow behind");
    RenderingEngine::release(lights);
    std::cout << "PASS production LightManager immutable-model token: stationary reuse, mutations/publication, unknown fallback, source removal\n";
}

static void layerContentIntegration()
{
    LightManager lights;
    Camera camera;
    GraphicsDataMisc graphics;
    graphics.current_camera = &camera;
    Engine::getPtrClassAddr()->lightManager = &lights;
    RenderingEngine::initialize(lights, graphics);
    lights.createDirectionalLight({-45,20,0},{1,1,1})->setshadow(true);
    LayeredTerrain terrain;
    std::vector<CustomRenderer*> providers{&terrain};
    require(RenderingEngine::frame(lights, providers) == 4, "Layer provider did not initialize all maps");
    const auto initial = lights.getShadowContentRevision();
    require(RenderingEngine::frame(lights, providers) == 0 && terrain.queries == 8,
        "Layer selection was not queried once per layer or failed stationary reuse");
    terrain.revisions[3] = nextShadowCasterBoundsRevision();
    require(RenderingEngine::frame(lights, providers) == 1 && lights.getShadowReusedLayers() == 3 &&
        lights.getShadowContentRevision() > initial && terrain.draws == std::array<unsigned,4>{1,1,1,2},
        "Far-layer publication redrew unrelated near maps or froze updated geometry");
    require(RenderingEngine::frame(lights, providers) == 0, "Updated far layer did not recache");
    terrain.revisions[1] = 0; // Only this provider/layer cannot promise immutable content.
    require(RenderingEngine::frame(lights, providers) == 1 &&
        RenderingEngine::frame(lights, providers) == 1,
        "Unknown layer either froze or unnecessarily invalidated every map");
    terrain.revisions[1] = nextShadowCasterBoundsRevision();
    require(RenderingEngine::frame(lights, providers) == 1 &&
        RenderingEngine::frame(lights, providers) == 0,
        "Returning to known layer content reused unknown data or failed to recache");
    Terrain legacy;
    providers.push_back(&legacy);
    require(RenderingEngine::frame(lights, providers) == 4, "Added legacy caster left old maps cached");
    require(RenderingEngine::frame(lights, providers) == 0, "Legacy default layer adapter failed reuse");
    legacy.publish();
    require(RenderingEngine::frame(lights, providers) == 4, "Global fallback failed conservative invalidation");
    providers.pop_back();
    require(RenderingEngine::frame(lights, providers) == 4, "Removed provider left its silhouette cached");
    for (auto& token : terrain.revisions) token = nextShadowCasterBoundsRevision();
    require(RenderingEngine::frame(lights, providers) == 4, "Same-address provider restart reused prior layer tokens");
    require(RenderingEngine::frame(lights, providers, true) == 4,
        "Layer providers hid an unknown ordinary caster or callback");
    RenderingEngine::release(lights);
    std::cout << "PASS production LightManager layer content: far publication redraws 1/4, exact source identities, unknown per-layer and global fallback, removal/restart\n";

    ShadowMapCache incomplete;
    const std::vector<glm::mat4> matrices(4, glm::mat4(1));
    const std::vector<std::vector<ShadowContentSource>> missing(3);
    for (unsigned frame = 0; frame < 2; ++frame)
    {
        incomplete.prepareLayerSources(matrices, missing, false);
        for (unsigned i = 0; i < 4; ++i)
        { require(incomplete.needsRender(i), "Malformed layer source list became cacheable"); incomplete.rendered(i); }
        incomplete.finish();
    }
}

int main()
{
    try { managerLifecycle(); localProjectionInvalidation(); individualLayers(); immutableModelIntegration(); layerContentIntegration(); }
    catch (const std::exception& e) { std::cerr << "FAIL shadow cache: " << e.what() << '\n'; return 1; }
}
