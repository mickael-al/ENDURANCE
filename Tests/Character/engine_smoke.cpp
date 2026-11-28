// Bounded integration scene: actual engine, imported character, materials,
// shadows, camera, bone publication and asynchronous cloth/hair. No terrain.
#include "glcore.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "GraphicsDataMisc.hpp"
#include "RenderingEngine.hpp"
#include "FrameBuffer.hpp"
#include "Skybox.hpp"
#include "EnduranceCharacter.hpp"
#include "EnduranceAtmosphere.hpp"
#include "Atmosphere.hpp"
#include "DirectionalLight.hpp"
#include "SkinnedTransform.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "Model.hpp"
#include "Materials.hpp"
#include "PathManager.hpp"
#include "hv/cpputil/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#if defined(__linux__)
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}

bool option(const char* name) { return std::getenv(name) != nullptr; }

bool finite(const glm::mat4& matrix)
{
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}

void checkProceduralPublicationPreservesPlayback()
{
    // Real runtime objects with a minimal in-memory asset: no test renderer,
    // imported animation or mutation of the character's shared FBX is needed.
    Skeleton skeleton;
    Bone root;
    root.name = "Root";
    root.bindPose[3] = glm::vec4(2.f, 3.f, 4.f, 1.f);
    root.inverseBindPose = glm::inverse(root.bindPose);
    skeleton.bones.push_back(root);
    std::vector<std::vector<Vertex>> vertices;
    std::vector<std::vector<std::uint32_t>> indices;
    Ge::ShapeBufferSkeleton buffer(vertices, indices, skeleton, nullptr);
    // The underlying object is non-const. The const API protects game callers;
    // this fixture supplies two synthetic clips directly to that same storage.
    auto& clips = const_cast<std::vector<Animation>&>(buffer.getAnimations());
    Animation clip;
    clip.name = "Fixture A";
    clip.max_duration = 1.0;
    clip.sampleCount = 2;
    clip.samples.resize(2);
    clips.push_back(clip);
    clip.name = "Fixture B";
    clips.push_back(clip);
    Ge::SkinnedTransform transform(&buffer, 0);
    require(transform.getGlobalPose().size() == 1 &&
        transform.getGlobalPose()[0] == root.bindPose,
        "A skeleton without clips must expose its complete bind pose immediately");
    require(transform.playAnimation(0), "Synthetic source clip did not start");
    auto composed = transform.getGlobalPose();
    composed[0][3].x += 0.5f;
    require(transform.setProceduralGlobalPose(composed) && transform.isAnimationPlaying() &&
        transform.getPrimaryAnimation() == 0,
        "Procedural publication stopped or changed the active clip");
    require(transform.blendAnimations(0, 1, .25f, .75f), "Synthetic clip blend did not start");
    require(transform.setProceduralGlobalPose(composed) && transform.isAnimationPlaying() &&
        transform.getPrimaryAnimation() == 0 && transform.getSecondaryAnimation() == 1 &&
        transform.getSecondaryWeight() == .75f,
        "Procedural publication changed clip blend state");
    require(!transform.setProceduralGlobalPose({}), "Incomplete procedural pose was accepted");
    auto invalid = composed;
    invalid[0][3].x = std::numeric_limits<float>::quiet_NaN();
    require(!transform.setProceduralGlobalPose(invalid) && transform.getGlobalPose() == composed &&
        transform.isAnimationPlaying() && transform.getSecondaryWeight() == .75f,
        "Rejected procedural pose changed the previous pose or playback state");
    require(transform.setExternalGlobalPose(composed) && !transform.isAnimationPlaying(),
        "Legacy external/ragdoll pose must continue to stop clip playback");
}

struct SmokeScene final : Scene, Behaviour
{
    EnduranceCharacterComponent character;
    EnduranceAtmosphereComponent atmosphere;
    fs::path output;
    std::vector<Ge::Lights*> lights;
    unsigned frame = 0;
    std::size_t baselineModels = 0;
    bool finished = false;
    Json report;

    explicit SmokeScene(fs::path folder) : output(std::move(folder)) {}

    void load() override
    {
        checkProceduralPublicationPreservesPlayback();
        report["procedural_pose_preserves_playback"] = true;
        const auto& pc = Ge::Engine::getPtrClass();
        // A controlled neutral studio environment illuminates the underside of
        // folds without relying on a terrain sky or an uninitialized cubemap.
        GLfloat previousClear[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClear);
        glClearColor(.25f, .25f, .25f, 1.f);
        pc.skybox->clearTextureSkybox();
        glClearColor(previousClear[0], previousClear[1], previousClear[2], previousClear[3]);
        baselineModels = pc.modelManager->getModels().size();
        lights.push_back(pc.lightManager->createDirectionalLight(
            {-38.0f, -40.0f, 0.0f}, {2.2f, 2.05f, 1.85f}, "Smoke key"));
        lights.back()->setshadow(!option("ENDURANCE_SMOKE_NO_SHADOWS"));
        lights.push_back(pc.lightManager->createDirectionalLight(
            {-10.0f, 140.0f, 0.0f}, {0.30f, 0.42f, 0.60f}, "Smoke fill"));
        lights.back()->setshadow(false);
        character.cameraYaw = 20.0f;
        character.cameraPitch = -8.0f;
        character.cameraDistance = 3.0f;
        character.cameraTargetHeight = 1.0f;
        character.useEnvironmentWind = false;
        character.clothPhysics = !option("ENDURANCE_SMOKE_NO_CLOTH");
        character.wind = {{3.0f, 0.0f, 1.0f}};
        if (option("ENDURANCE_SMOKE_ATMOSPHERE"))
        {
            atmosphere.engineSettings = Json{{"enabled", true}, {"clouds", false},
                {"moonSurface", false}, {"quality", 0}}.dump();
            pc.behaviourManager->addBehaviour(&atmosphere);
        }
        pc.behaviourManager->addBehaviour(&character);
        pc.behaviourManager->addBehaviour(this);
    }

    void unload() override
    {
        const auto& pc = Ge::Engine::getPtrClass();
        pc.behaviourManager->removeBehaviour(this, true);
        pc.behaviourManager->removeBehaviour(&character, true);
        character.stop();
        if (option("ENDURANCE_SMOKE_ATMOSPHERE"))
            pc.behaviourManager->removeBehaviour(&atmosphere, true);
        require(pc.modelManager->getModels().size() == baselineModels,
                "Character stop leaked its rendered models");
        for (auto* light : lights) pc.lightManager->destroyLight(light);
        lights.clear();
    }

    void start() override {}
    void stop() override {}
    void fixedUpdate() override {}
    void onGUI() override {}

    void checkPose()
    {
        unsigned skeletonCount = 0;
        for (auto* object : Ge::GObject::GetGObjects())
        {
            auto* skinned = dynamic_cast<Ge::SkinnedTransform*>(object);
            if (!skinned) continue;
            ++skeletonCount;
            const auto previous = skinned->getGlobalPose();
            require(!previous.empty(), "Character has no evaluated bind/procedural pose");
            for (const auto& matrix : previous) require(finite(matrix), "Non-finite global bone pose");
            require(!skinned->setProceduralGlobalPose({}), "Invalid empty procedural pose accepted");
            auto invalid = previous;
            invalid.front()[0][0] = std::numeric_limits<float>::quiet_NaN();
            require(!skinned->setProceduralGlobalPose(invalid), "Invalid NaN procedural pose accepted");
            require(skinned->getGlobalPose() == previous, "Rejected pose changed current skeleton");
        }
        require(skeletonCount == 1, "Expected one shared character skeleton");
    }

    void capture(const char* name)
    {
        const auto& pc = Ge::Engine::getPtrClass();
        int width = 0, height = 0;
        glfwGetFramebufferSize(pc.gdm->str_window, &width, &height);
        require(width > 0 && height > 0, "Empty smoke framebuffer");
        // Read the production shaded HDR target, which remains defined after
        // GLFW swaps. GL_FRONT on an unmapped Xvfb window can contain undefined
        // pixels; accepting that would turn memory noise into a false pass.
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
        GLint previousFramebuffer = 0, previousPack = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPack);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // Depth identifies the whole actor (including opaque forward jade),
        // independent of whether the background is plain studio or real sky.
        std::vector<float> depth(static_cast<std::size_t>(width) * height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, pc.renderingEngine->getFrameBuffer()->getFrameBuffer());
        glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousFramebuffer);
        auto readTarget = [&](unsigned buffer, unsigned attachment, const char* suffix, unsigned mode) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, buffer);
            GLint previousReadBuffer = 0;
            glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
            glReadBuffer(attachment);
            std::vector<float> values(static_cast<std::size_t>(width) * height * 3);
            glReadPixels(0, 0, width, height, GL_RGB, GL_FLOAT, values.data());
            glReadBuffer(previousReadBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, previousFramebuffer);
            std::size_t invalid = 0;
            float maximum = 0.0f;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                float value = values[i];
                if (!std::isfinite(value)) { ++invalid; value = 0.0f; }
                maximum = std::max(maximum, value);
                if (mode == 0) value = 1.0f - std::exp(-std::max(value, 0.0f));
                if (mode == 2) value = value * 0.5f + 0.5f;
                else value = std::pow(std::clamp(value, 0.0f, 1.0f), 1.0f / 2.2f);
                pixels[i] = static_cast<unsigned char>(255.0f * value);
            }
            std::cerr << name << suffix << ": nonfinite=" << invalid << " maximum=" << maximum << '\n';
            std::ofstream file(output / (std::string(name) + suffix + ".ppm"), std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (int y = height - 1; y >= 0; --y)
                file.write(reinterpret_cast<const char*>(pixels.data() + std::size_t(y) * width * 3), width * 3);
            require(file.good(), "Cannot write smoke frame");
            require(invalid == 0, "Non-finite rendered framebuffer values");
            if (mode == 0)
            {
                std::size_t foreground = 0;
                int minX = width, minY = height, maxX = 0, maxY = 0;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const float z = depth[std::size_t(y) * width + x];
                        require(std::isfinite(z), "Non-finite character framebuffer depth");
                        if (z >= .999999f) continue;
                        ++foreground;
                        minX = std::min(minX, x); maxX = std::max(maxX, x);
                        minY = std::min(minY, y); maxY = std::max(maxY, y);
                    }
                const float coverage = static_cast<float>(foreground) / (width * height);
                require(coverage > 0.01f && coverage < 0.5f,
                        "Character framebuffer is blank or its background is corrupt");
                require(minX > 0 && minY > 0 && maxX < width - 1 && maxY < height - 1,
                        "Character render escaped the bounded preview camera");
                require(maxY - minY > height / 3, "Character render is unexpectedly small");
                report["captures"][name] = {{"foreground_fraction", coverage},
                    {"bounds", {minX, minY, maxX, maxY}}, {"finite_hdr", true}};
            }
        };
        const auto* fb = pc.renderingEngine->getFrameBuffer();
        readTarget(fb->getFowardFrameBuffer(), GL_COLOR_ATTACHMENT0, "", 0);
        readTarget(fb->getFrameBuffer(), GL_COLOR_ATTACHMENT2, "-albedo", 1);
        readTarget(fb->getFrameBuffer(), GL_COLOR_ATTACHMENT1, "-normals", 2);
        glPixelStorei(GL_PACK_ALIGNMENT, previousPack);
        require(glGetError() == GL_NO_ERROR, "OpenGL readback or preceding render error");
    }

    void update() override
    {
        const auto& pc = Ge::Engine::getPtrClass();
        require(character.ready(), "Character did not initialize: " + character.error());
        require(character.error().empty(), "Character diagnostic error: " + character.error());
        require(character.meshCount() == 15, "Imported character lost one or more of its 15 meshes");
        require(character.hairBoneCount() > 0, "Hair bone simulation is empty");
        require(character.clothParticleCount() > 0, "Cloth simulation is empty");
        ++frame;
        const unsigned warmup = option("ENDURANCE_SMOKE_ATMOSPHERE") ? 60 : 0;
        if (frame == 1 && option("ENDURANCE_SMOKE_NO_NORMAL_MAPS"))
            for (auto* model : pc.modelManager->getModels()) model->getMaterial()->setNormal(0.0f);
        if (frame == 36 + warmup)
        {
            if (option("ENDURANCE_SMOKE_ATMOSPHERE"))
            {
                const auto* sky = pc.renderingEngine->getAtmosphere();
                require(sky->settings().enabled && sky->stats().environmentUpdates > 0,
                        "Actual game atmosphere did not publish its image-based lighting");
            }
            require(!character.clothPhysics || character.readyClothCount() == 2,
                    "Dress/belt worker results never reached the renderer");
            checkPose();
            capture("front");
            character.cameraYaw = 155.0f;
        }
        if (frame == 52 + warmup)
        {
            checkPose();
            capture("back");
            report.update({{"scope", "actual engine, isolated character scene, no terrain"},
                      {"capture", "production forward HDR target, exposure 1 / gamma 2.2 preview"},
                      {"renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER))},
                      {"opengl", reinterpret_cast<const char*>(glGetString(GL_VERSION))},
                      {"meshes", character.meshCount()}, {"hair_bones", character.hairBoneCount()},
                      {"cloth_particles", character.clothParticleCount()},
                      {"ready_cloth", character.readyClothCount()},
                      {"cloth_enabled", character.clothPhysics},
                      {"atmosphere_enabled", option("ENDURANCE_SMOKE_ATMOSPHERE")},
                      {"shadows_enabled", !option("ENDURANCE_SMOKE_NO_SHADOWS")},
                      {"normal_maps_enabled", !option("ENDURANCE_SMOKE_NO_NORMAL_MAPS")},
                      {"imported_body_height", character.importedBodyHeight()},
                      {"target_height_m", character.height}});
            // Exercise stop/restart while the worker pool has been active.
            character.stop();
            require(pc.modelManager->getModels().size() == baselineModels,
                    "Character restart retained old models");
            character.cameraYaw = 20.0f;
            character.start();
            require(character.ready(), "Character restart failed: " + character.error());
            if (option("ENDURANCE_SMOKE_NO_NORMAL_MAPS"))
                for (auto* model : pc.modelManager->getModels()) model->getMaterial()->setNormal(0.0f);
        }
        if (frame >= 76 + warmup)
        {
            checkPose();
            require(!character.clothPhysics || character.readyClothCount() == 2,
                    "Cloth did not resume after restart");
            report["restart_passed"] = true;
            report["frames"] = frame;
            std::ofstream file(output / "report.json");
            file << report.dump(2) << '\n';
            require(file.good(), "Cannot write smoke report");
            finished = true;
            glfwSetWindowShouldClose(pc.gdm->str_window, GLFW_TRUE);
        }
    }
};
}

int main(int argc, char** argv)
{
#if defined(__linux__)
    std::signal(SIGSEGV, [](int signal) {
        void* frames[48];
        const int count = backtrace(frames, 48);
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
        _exit(128 + signal);
    });
#endif
    std::cout << std::unitbuf;
    try
    {
        require(argc == 3, "Usage: character-engine-smoke repository-root output-directory");
        const auto root = fs::absolute(argv[1]);
        const auto output = fs::absolute(argv[2]);
        fs::create_directories(output);
        fs::current_path(root / "Bin");
        PathManager::setHomeProjectPath((root / "ENDURANCE").string());
        std::cerr << "Constructing actual engine\n";
        Ge::Engine engine;
        const auto& pc = Ge::Engine::getPtrClass();
        pc.settingManager->setName("ENDURANCE character integration smoke");
        pc.settingManager->setEditor(false);
        pc.settingManager->setWindowWidth(800);
        pc.settingManager->setWindowHeight(900);
        pc.settingManager->setFps(60.0);
        pc.settingManager->setVSync(false);
        pc.settingManager->setSsaoEnabled(false);
        pc.settingManager->setClearColor({0.11f, 0.14f, 0.18f, 1.0f});
        pc.settingManager->setAmbient(0.35f);
        std::cerr << "Initializing actual engine\n";
        require(engine.initialize(), "Actual engine initialization failed");
        pc.postProcess->getPPSetting()->bloom = false;
        pc.postProcess->getPPSetting()->exposure = 1.0f;
        SmokeScene scene(output);
        pc.sceneManager->addScene("character-smoke", &scene);
        engine.start();
        require(scene.finished, "Smoke window closed before checks completed");
        engine.release();
        std::cout << "Character engine smoke passed: " << (output / "report.json") << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Character engine smoke failed: " << error.what() << '\n';
        return 1;
    }
}
