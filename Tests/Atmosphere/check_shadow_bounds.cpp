#include "glcore.hpp"
#include "ShadowCasterBounds.hpp"
#include "CustomRenderer.hpp"
#include "LightManager.hpp"
#include "DirectionalLight.hpp"
#include "SpotLight.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "GraphicsDataMisc.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>

using namespace Ge;

namespace Ge
{
    // Production LightManager grants the renderer access to these operations.
    class RenderingEngine
    {
    public:
        static void initialize(LightManager& lights, GraphicsDataMisc& graphics) { lights.initialize(&graphics); }
        static void prepare(LightManager& lights, const std::vector<CustomRenderer*>& providers) { lights.prepareShadowCasterBounds(providers); }
        static void release(LightManager& lights) { lights.release(); }
    };
}

static void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

struct PublishedCaster final : CustomRenderer
{
    std::vector<ShadowCasterBounds> boxes;
    std::uint64_t revision = nextShadowCasterBoundsRevision();
    mutable unsigned copies = 0;
    void drawRenderer() override {}
    std::uint64_t shadowCasterBoundsRevision() const override { return revision; }
    void appendShadowCasterBounds(std::vector<ShadowCasterBounds>& out) const override
    { ++copies; out.insert(out.end(), boxes.begin(), boxes.end()); }
    void publish(std::vector<ShadowCasterBounds> value)
    { boxes = std::move(value); revision = nextShadowCasterBoundsRevision(); }
};

static ShadowCasterBounds boxAt(glm::dvec3 center, glm::dvec3 half = glm::dvec3(1.0))
{ return {center - half, center + half}; }

static void independentGeometryOracle()
{
    DirectionalShadowVolume volume;
    volume.direction = glm::dvec3(0, 0, 1);
    volume.radius = 100;
    require(fitDirectionalShadowCasterMargin(volume, 4000, {}) == 4000, "Missing bounds reduced the explicit margin");
    require(fitDirectionalShadowCasterMargin(volume, 16000, {boxAt({0, 0, -6000})}) == 16000,
        "Adaptive fitting reduced an explicit user margin");
    for (const auto& box : {boxAt({102, 0, -16000}), boxAt({0, -102, -16000}), boxAt({0, 0, 102})})
        require(fitDirectionalShadowCasterMargin(volume, 4000, {box}) == 4000,
            "An irrelevant XY/downstream caster expanded the cascade");
    require(fitDirectionalShadowCasterMargin(volume, 4000, {boxAt({101, 0, -7000})}) > 6800,
        "Boundary-contact caster was rejected");

    std::mt19937 random(8717);
    std::uniform_real_distribution<double> component(-1, 1), distance(5000, 16000), extent(.01, 120);
    std::size_t corners = 0;
    for (const glm::dvec3 world : {glm::dvec3(0), glm::dvec3(40000, 1700, -38000), glm::dvec3(1000000, -500000, 250000)})
    for (unsigned test = 0; test < 96; ++test)
    {
        volume.center = world;
        volume.direction = glm::normalize(glm::dvec3(component(random), component(random), component(random)));
        const glm::dvec3 reference = std::abs(volume.direction.y) < .99 ? glm::dvec3(0, 1, 0) : glm::dvec3(0, 0, 1);
        volume.right = glm::normalize(glm::cross(volume.direction, reference));
        volume.up = glm::cross(volume.right, volume.direction);
        volume.radius = 256;
        const auto box = boxAt(world - volume.direction * distance(random), glm::dvec3(extent(random), extent(random), extent(random)));
        const float margin = fitDirectionalShadowCasterMargin(volume, 4000, {box});
        // Independent clip-space oracle: project the eight box corners through
        // an actual orthographic camera, not the fitting function's support test.
        const auto view = glm::lookAt(world - volume.direction * (volume.radius + margin), world, volume.up);
        const auto projection = glm::ortho(-volume.radius, volume.radius, -volume.radius, volume.radius,
            0.0, 2.0 * volume.radius + margin);
        for (unsigned i = 0; i < 8; ++i)
        {
            const glm::dvec4 point((i & 1) ? box.maximum.x : box.minimum.x,
                (i & 2) ? box.maximum.y : box.minimum.y, (i & 4) ? box.maximum.z : box.minimum.z, 1);
            const auto clip = projection * (view * point);
            require(clip.z >= -1.0 && clip.z <= 1.0, "Fitted depth clipped a loaded caster corner");
            ++corners;
        }
    }
    const auto invalid = ShadowCasterBounds{{0, 0, std::numeric_limits<double>::quiet_NaN()}, {1, 1, 1}};
    require(!validShadowCasterBounds(invalid), "Nonfinite bounds passed validation");
    require(!validShadowCasterBounds({{1, 0, 0}, {0, 1, 1}}), "Inverted bounds passed validation");
    std::cout << "PASS conservative caster geometry: " << corners << " independently projected corners, 288 orientations/origins; XY/downstream rejection, contacts, explicit floor and invalid data\n";
}

static glm::dvec3 cascadeCenter(const Lights& light, const ShadowMatrix& matrix, unsigned cascade)
{
    const glm::dvec3 row(matrix.projview[0][0], matrix.projview[1][0], matrix.projview[2][0]);
    const double radius = 1.0 / glm::length(row);
    return glm::dvec3(matrix.pos) + glm::normalize(glm::dvec3(light.getDirection()))
        * (radius + light.getDirectionalShadowCasterMargins()[cascade]);
}

static bool depthContains(const glm::mat4& matrix, glm::dvec3 point)
{
    const auto clip = glm::dmat4(matrix) * glm::dvec4(point, 1);
    return clip.z >= -clip.w && clip.z <= clip.w;
}

static void productionPublicationAndBias()
{
    LightManager lights;
    Camera camera;
    GraphicsDataMisc graphics;
    graphics.current_camera = &camera;
    Engine::getPtrClassAddr()->lightManager = &lights;
    RenderingEngine::initialize(lights, graphics);
    auto* sun = lights.createDirectionalLight({-20, 37, 0}, {1, 1, 1});
    sun->setshadow(true);
    sun->setShadowCasterMargin(4000);
    const auto initial = sun->getShadowMatrix();
    const auto center = cascadeCenter(*sun, initial[0], 0);
    const auto direction = glm::normalize(glm::dvec3(sun->getDirection()));
    const auto mountain = center - direction * 7000.0;
    require(!depthContains(initial[0].projview, mountain), "Off-screen mountain fixture was already in the 4 km volume");
    PublishedCaster terrain;
    terrain.publish({boxAt(mountain)});
    std::vector<CustomRenderer*> providers{&terrain};
    const auto allocations = FixtureGL::textureAllocations;
    RenderingEngine::prepare(lights, providers);
    require(depthContains(sun->getShadowMatrix()[0].projview, mountain), "Published mountain was clipped by the actual directional matrix");
    require(sun->getDirectionalShadowCasterMargins()[0] > 4000, "Actual light did not extend its first cascade");
    require(FixtureGL::textureAllocations == allocations, "Fitting allocated new GPU shadow textures");
    for (unsigned cascade = 0; cascade < SHADOW_MAP_CASCADE_COUNT; ++cascade)
    {
        const auto& updated = sun->getShadowMatrix()[cascade];
        require(updated.splitDepth == initial[cascade].splitDepth, "Caster fitting changed receiver cascade splits");
        for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 2; ++row)
            require(updated.projview[column][row] == initial[cascade].projview[column][row], "Changing only caster depth changed XY mapping");
    }
    const auto writes = FixtureGL::writes[graphics.str_ssbo.str_shadow];
    for (unsigned frame = 0; frame < 64; ++frame) RenderingEngine::prepare(lights, providers);
    require(terrain.copies == 1, "Unchanged terrain recopied bounds each frame");
    require(FixtureGL::writes[graphics.str_ssbo.str_shadow] == writes, "Unchanged publication rebuilt or uploaded shadow matrices");

    const auto farther = center - direction * 13000.0;
    terrain.publish({boxAt(farther)});
    RenderingEngine::prepare(lights, providers);
    require(depthContains(sun->getShadowMatrix()[0].projview, farther), "Stationary publication failed to extend the actual light matrix");
    require(FixtureGL::writes[graphics.str_ssbo.str_shadow] > writes, "Stationary publication did not refresh the shadow SSBO");
    sun->setShadowCasterMargin(16000);
    RenderingEngine::prepare(lights, providers);
    for (float margin : sun->getDirectionalShadowCasterMargins()) require(margin >= 16000, "Fitting reduced a saved 16 km user setting");
    sun->setShadowCasterMargin(4000);
    RenderingEngine::prepare(lights, {});
    for (float margin : sun->getDirectionalShadowCasterMargins()) require(margin == 4000, "Unregistering bounds retained a stale extension");

    // The actual setter updates the packed 80-byte light buffer, independently
    // for each light, and performs no upload when its clamped result is equal.
    require(sun->getDirectionalShadowBias() == glm::vec2(.5f, 1.5f), "Directional bias defaults changed");
    sun->setDirectionalShadowBias(-2, 50);
    require(sun->getDirectionalShadowBias() == glm::vec2(0, 8), "Actual bias setter did not clamp to [0,8]");
    auto lightWrites = FixtureGL::writes[graphics.str_ssbo.str_light];
    sun->setDirectionalShadowBias(-3, 100);
    require(FixtureGL::writes[graphics.str_ssbo.str_light] == lightWrites, "Unchanged clamped bias uploaded the light again");
    sun->setDirectionalShadowBias(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    require(sun->getDirectionalShadowBias() == glm::vec2(.5f, 1.5f), "Nonfinite bias did not restore safe defaults");
    sun->setDirectionalShadowBias(0, 0);
    require(sun->getDirectionalShadowBias() == glm::vec2(0), "Explicit zero bias was replaced by defaults");
    UniformBufferLight uploaded{};
    const auto& bytes = FixtureGL::buffers.at(graphics.str_ssbo.str_light);
    std::memcpy(&uploaded, bytes.data() + sun->getIndex() * sizeof(uploaded), sizeof(uploaded));
    require(uploaded.shadowDepthBias == 0 && uploaded.shadowNormalBias == 0, "Bias setter failed to publish the actual light data");
    std::cout << "PASS production Lights/LightManager: 4 km clipping reproduced, published off-screen mountains retained, stationary cache reuse/invalidation, identical XY/splits, no GPU allocation, manual floor, unregister and actual bias upload/clamp/nonfinite/zero\n";
    RenderingEngine::release(lights);
    Engine::getPtrClassAddr()->lightManager = nullptr;
}

static void restartedProviderAndAggregate()
{
    ShadowCasterBoundsCache cache;
    PublishedCaster other;
    other.publish({boxAt({3, 4, 5})});
    alignas(PublishedCaster) unsigned char storage[sizeof(PublishedCaster)];
    auto* provider = new (storage) PublishedCaster;
    provider->publish({boxAt({1, 2, 3})});
    std::vector<CustomRenderer*> providers{provider, &other};
    require(cache.update(providers) && cache.bounds().size() == 2, "Providers did not aggregate");
    require(!cache.update(providers) && other.copies == 1, "Unchanged providers invalidated cache");
    provider->~PublishedCaster();
    provider = new (storage) PublishedCaster;
    provider->publish({boxAt({17000, 2, 3})});
    providers[0] = provider;
    require(cache.update(providers), "Restart at the same provider address reused stale bounds");
    require(cache.bounds()[0].minimum.x == 16999 && provider->copies == 1 && other.copies == 1,
        "Changed provider did not replace its bounds independently");
    provider->~PublishedCaster();
    require(cache.update(std::vector<CustomRenderer*>{&other}) && cache.bounds().size() == 1,
        "Removed provider remained in aggregate");
    std::cout << "PASS publication lifecycle: independent provider revisions, aggregate removal and restart at the same address\n";
}

static void productionStableCascadesAndSpot()
{
    LightManager lights;
    Camera camera;
    GraphicsDataMisc graphics;
    graphics.current_camera = &camera;
    Engine::getPtrClassAddr()->lightManager = &lights;
    RenderingEngine::initialize(lights, graphics);
    auto* sun = lights.createDirectionalLight({-35, 37, 0}, {1, 1, 1});
    sun->setshadow(true);
    sun->setShadowCasterMargin(4000);
    unsigned cornersChecked=0, stationaryPairs=0;
    for (bool orthographic : {false, true})
    for (const auto origin : {glm::vec3(0),glm::vec3(40000,1700,-38000)})
    for (const auto angles : {glm::vec3(0),glm::vec3(67,133,0),glm::vec3(-23,-71,0)})
    {
        camera.orthographic=orthographic; camera.position=origin; camera.angles=angles;
        camera.notify(lights);
        const auto initial=sun->getShadowMatrix();
        const auto invP=glm::inverse(glm::dmat4(camera.getProjectionMatrix()));
        const auto invV=glm::inverse(glm::dmat4(camera.getViewMatrix()));
        for(unsigned cascade=0;cascade<4;++cascade)
        {
            const double end=-initial[cascade].splitDepth;
            const double previousEnd=cascade ? -initial[cascade-1].splitDepth : camera.getNear();
            const double previousStart=cascade>1 ? -initial[cascade-2].splitDepth : 0;
            const double start=cascade ? (std::max)(double(camera.getNear()),previousEnd-.1*(previousEnd-previousStart)) : camera.getNear();
            for(double depth : {start,end}) for(int x : {-1,1}) for(int y : {-1,1})
            {
                auto a=invP*glm::dvec4(x,y,-1,1), b=invP*glm::dvec4(x,y,0,1);
                a/=a.w;b/=b.w;
                const auto viewPoint=a+(b-a)*((-depth-a.z)/(b.z-a.z));
                const auto clip=glm::dmat4(initial[cascade].projview)*invV*viewPoint;
                require(std::abs(clip.x)<clip.w && std::abs(clip.y)<clip.w &&
                    std::abs(clip.z)<clip.w, "Snapped cascade clips a receiver or a crossfade corner");
                ++cornersChecked;
            }
        }
        const glm::vec3 row(initial[0].projview[0][0],initial[0].projview[1][0],initial[0].projview[2][0]);
        const float texel=2.0f/(glm::length(row)*TEXTURE_DIM);
        const auto direction=glm::normalize(row);
        auto previous=initial[0].projview;
        for(unsigned step=1;step<=20;++step)
        {
            camera.position=origin+direction*(texel*.05f*step);
            camera.notify(lights);
            const auto matrix=sun->getShadowMatrix()[0].projview;
            bool stable=true;
            for(unsigned column=0;column<4;++column) for(unsigned axis=0;axis<2;++axis)
                stable=stable && matrix[column][axis]==previous[column][axis];
            stationaryPairs+=stable;
            for(unsigned axis=0;axis<2;++axis)
            {
                const double shift=(double(matrix[3][axis])-previous[3][axis])*TEXTURE_DIM*.5;
                require(std::abs(shift-std::round(shift))<.002, "Cascade translation was not snapped to whole shadow texels");
            }
            for(unsigned column=0;column<3;++column) for(unsigned axis=0;axis<2;++axis)
                require(matrix[column][axis]==initial[0].projview[column][axis], "Camera translation changed cascade scale or orientation");
            previous=matrix;
        }
    }
    require(stationaryPairs>180, "Subtexel camera motion still moves the shadow grid continuously");

    auto* spot=lights.createSpotLight({1000,300,-800},{1,1,1},{0,0,0},60);
    spot->setshadow(true);
    require(SHADOW_MAP_SPOT_COUNT==1 && spot->getShadowMatrix().size()==1,
        "Spotlight still allocates directional cascades");
    require(lights.getShadowMatrix().size()==5, "Packed spotlight consumes more than one shadow layer");
    for(const auto angles : {glm::vec3(0),glm::vec3(90,0,0),glm::vec3(-90,0,0),glm::vec3(13,79,0)})
    {
        spot->setEulerAngles(angles);
        const auto matrix=glm::dmat4(spot->getShadowMatrix()[0].projview);
        const auto position=glm::dvec3(1000,300,-800);
        const auto direction=glm::normalize(glm::dvec3(spot->getDirection()));
        const auto up=std::abs(direction.y)>.99 ? glm::dvec3(0,0,1) : glm::dvec3(0,1,0);
        const auto right=glm::normalize(glm::cross(direction,up));
        const auto inside=matrix*glm::dvec4(position+direction*100.0,1);
        const auto behind=matrix*glm::dvec4(position-direction*100.0,1);
        const double radius=std::tan(glm::radians(30.0))*100;
        const auto edge=matrix*glm::dvec4(position+direction*100.0+right*radius,1);
        const auto outside=matrix*glm::dvec4(position+direction*100.0+right*(radius*1.1),1);
        require(inside.w>0 && std::abs(inside.x/inside.w)<1e-4 && std::abs(inside.y/inside.w)<1e-4 &&
            std::abs(inside.z)<inside.w, "Spotlight axis is clipped or off-centre");
        require(behind.w<0 && std::abs(std::abs(edge.x/edge.w)-1)<1e-4 && std::abs(outside.x)>outside.w,
            "Perspective spotlight cone or rear rejection is incorrect");
    }
    spot->setSpotAngle(0); require(spot->getSpotAngle()==1,"Degenerate spotlight angle accepted");
    spot->setSpotAngle(360); require(spot->getSpotAngle()==179,"Inverted spotlight angle accepted");
    spot->setSpotAngle(std::numeric_limits<float>::quiet_NaN());
    require(spot->getSpotAngle()==45,"Invalid spotlight angle did not recover");
    std::cout << "PASS production cascade stability: " << cornersChecked << " perspective/ortho/rotated/far-origin overlap corners; "
        << stationaryPairs << "/240 stable subtexel pairs, integer texel shifts; single-layer spotlight cone and pole directions\n";
    RenderingEngine::release(lights);
    Engine::getPtrClassAddr()->lightManager=nullptr;
}

int main()
{
    try { independentGeometryOracle(); productionPublicationAndBias(); restartedProviderAndAggregate(); productionStableCascadesAndSpot(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
