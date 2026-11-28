#include "EnduranceCharacter.hpp"
#define CLEAR_REGISTRY
#include "EnduranceCharacterAsset.hpp"
#include "EnduranceCharacterPose.hpp"
#include "EnduranceBiomeEffects.hpp"
#include "EnduranceParticleWind.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "GraphicsDataMisc.hpp"
#include "Empty.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "ShapeBufferBase.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "SkinnedTransform.hpp"
#include "SkinnedCloth.hpp"
#include "Materials.hpp"
#include "GraphiquePipeline.hpp"
#include "ThreadPool.hpp"
#include "Debug.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <limits>
#include <map>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

REGISTER(Behaviour, EnduranceCharacterComponent, enabled, hairPhysics, clothPhysics,
    useEnvironmentWind, modelFile, height, spawnPosition, cameraDistance, cameraYaw,
    cameraPitch, cameraTargetHeight, cameraFov, wind, windStrength, waistHeight,
    dressReleaseDistance, dressParticles, beltParticles);

namespace
{
using namespace EnduranceCharacter;
EnduranceCharacterComponent* activeCharacter = nullptr;
GLFWscrollfun previousScroll = nullptr;
double scrollDelta = 0.0;

void characterScroll(GLFWwindow* window, double x, double y)
{
    if (previousScroll) previousScroll(window, x, y);
    if (activeCharacter && (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureMouse))
        scrollDelta += y;
}

bool finite(float x)
{
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}
float safe(float x, float fallback, float lo, float hi)
{ return finite(x) ? std::clamp(x, lo, hi) : fallback; }
bool finite(const glm::vec3& v) { return finite(v.x) && finite(v.y) && finite(v.z); }

std::string resourcePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    if (std::filesystem::exists(path)) return path;
    if (path.rfind("../", 0) == 0 && std::filesystem::exists(path.substr(3))) return path.substr(3);
    return path;
}

int boneIndex(const Skeleton& skeleton, const char* name)
{
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        if (skeleton.bones[i].name == name) return static_cast<int>(i);
    return -1;
}

std::vector<ClothCapsule> bodyCapsules(const Skeleton& skeleton,
    const std::vector<glm::mat4>& pose, const glm::mat4& model, float height)
{
    std::vector<ClothCapsule> result;
    const glm::mat4 sourceToWorld = model * skeleton.inverseBindTransform;
    const float scale = height / 1.70f;
    auto add = [&](const char* from, const char* to, float radius)
    {
        const int a = boneIndex(skeleton, from), b = boneIndex(skeleton, to);
        if (a < 0 || b < 0 || pose.size() != skeleton.bones.size()) return;
        ClothCapsule c;
        c.a = glm::vec3(sourceToWorld * pose[a][3]);
        c.b = glm::vec3(sourceToWorld * pose[b][3]);
        c.radius = radius * scale;
        c.response = ClothContactResponse::SeparationOnly;
        result.push_back(c);
    };
    add("J_Bip_C_Hips", "J_Bip_C_UpperChest", .105f);
    // The skirt collides with the trouser envelope, not the bare thighs. These
    // radii fit the authored pants while leaving the pinned waist untouched.
    add("J_Bip_L_UpperLeg", "J_Bip_R_UpperLeg", .135f);
    for (const char* side : {"L", "R"})
    {
        const std::string prefix = std::string("J_Bip_") + side + "_";
        add((prefix + "UpperLeg").c_str(), (prefix + "LowerLeg").c_str(), .130f);
        add((prefix + "LowerLeg").c_str(), (prefix + "Foot").c_str(), .043f);
        add((prefix + "UpperArm").c_str(), (prefix + "LowerArm").c_str(), .040f);
        add((prefix + "LowerArm").c_str(), (prefix + "Hand").c_str(), .032f);
    }
    return result;
}
}

struct EnduranceCharacterComponent::Impl
{
    struct ClothPart
    {
        std::size_t shape = 0, particles = 0;
        Part part = Part::Unknown;
        std::unique_ptr<Ge::SkinnedCloth> simulation;
        bool drawingCloth = false;
    };
    struct HairResult
    {
        bool valid = false;
        std::uint64_t epoch = 0;
        std::vector<glm::mat4> pose;
        glm::mat4 baseHead{1.0f};
        double milliseconds = 0.0;
        float lengthError = 0.0f;
    };
    const ptrClass* pc = nullptr;
    Ge::ShapeBufferSkeleton* buffer = nullptr;
    Ge::SkinnedTransform* skeleton = nullptr;
    std::unique_ptr<Ge::Empty> root, pivot;
    Ge::Camera* camera = nullptr;
    std::vector<Ge::Model*> models;
    std::vector<Ge::Materials*> materials;
    std::map<std::string, Ge::Textures*> textures;
    std::vector<Ge::GraphiquePipeline*> pipelines;
    Ge::GraphiquePipeline *jade = nullptr, *face = nullptr, *eyes = nullptr;
    Ge::GraphiquePipeline *fabric = nullptr, *cloth = nullptr, *skinShadow = nullptr, *clothShadow = nullptr;
    std::unique_ptr<ThreadPool> workers;
    std::shared_ptr<EnduranceCharacter::Pose> hair;
    std::future<HairResult> hairJob;
    HairResult lastHair;
    std::vector<std::size_t> hairIndices;
    std::vector<glm::mat4> basePose;
    std::vector<ClothPart> clothes;
    EnduranceCharacter::Normalization normalization;
    EnduranceCharacter::AttachmentLayout attachments;
    std::vector<EnduranceCharacter::Part> parts;
    std::string error, warning;
    int head = -1;
    std::uint64_t epoch = 1;
    double seconds = 0.0, hairSeconds = 0.0;
    std::size_t hairBones = 0;
    bool registered = false, hairReset = true, wasHairEnabled = true, wasClothEnabled = true;
    float elapsedHair = 0.0f;
};

EnduranceCharacterComponent::EnduranceCharacterComponent() : m(std::make_unique<Impl>()) {}
EnduranceCharacterComponent::~EnduranceCharacterComponent() { stop(); }
bool EnduranceCharacterComponent::ready() const { return m->registered; }
const std::string& EnduranceCharacterComponent::error() const { return m->error; }
std::size_t EnduranceCharacterComponent::meshCount() const { return m->models.size(); }
std::size_t EnduranceCharacterComponent::hairBoneCount() const { return m->hairBones; }
float EnduranceCharacterComponent::importedBodyHeight() const { return m->normalization.bodyHeight; }
std::size_t EnduranceCharacterComponent::clothParticleCount() const
{
    std::size_t n = 0;
    for (const auto& c : m->clothes) n += c.particles;
    return n;
}
std::size_t EnduranceCharacterComponent::readyClothCount() const
{
    std::size_t n = 0;
    for (const auto& c : m->clothes) if (c.simulation->outputReady()) ++n;
    return n;
}

void EnduranceCharacterComponent::start()
{
    stop();
    if (!enabled) return;
    if (activeCharacter) { m->error = "Only one ENDURANCE character camera may own this scene."; return; }
    m->pc = Ge::Engine::getPtrClassAddr();
    if (!m->pc || !m->pc->gdm || !m->pc->modelManager || !m->pc->skinnedTransformManager)
    { m->error = "Character requires an initialized renderer."; return; }
    auto fail = [&](const std::string& message)
    {
        stop(); m->error = message;
        Ge::Debug::Warn("ENDURANCE character: %s", message.c_str());
    };
    try
    {
        const std::string path = resourcePath(modelFile);
        if (!std::filesystem::exists(path)) { fail("Character FBX does not exist: " + path); return; }
        m->buffer = m->pc->modelManager->allocateFBXBufferBones(path.c_str());
        if (!m->buffer || m->buffer->getSkeleton().bones.empty() || m->buffer->getShapes().empty())
        { fail("Character FBX has no usable skinned meshes."); return; }
        const auto& shapes = m->buffer->getShapes();
        const auto& metadata = m->buffer->getMeshMetadata();
        if (metadata.size() != shapes.size()) { fail("Character FBX material metadata is incomplete."); return; }
        std::vector<AssetMeshView> meshViews;
        for (std::size_t i = 0; i < shapes.size(); ++i)
        {
            m->parts.push_back(classify(metadata[i].meshName, metadata[i].materialName));
            meshViews.push_back({m->parts.back(), &shapes[i]->getVertices()});
        }
        height = safe(height, 1.7f, .3f, 5.0f);
        m->normalization = normalizeCharacter(meshViews, m->buffer->getSkeleton().bindTransform, height);
        if (!m->normalization.valid) { fail(m->normalization.error); return; }
        AttachmentSettings maskSettings;
        maskSettings.waistHeightMetres = waistHeight;
        maskSettings.dressFreeBelowWaist = safe(dressReleaseDistance, .20f, .04f, .5f) / height;
        m->attachments = attachmentLayout(meshViews, m->normalization, maskSettings);

        m->root = std::make_unique<Ge::Empty>();
        m->root->setName("ENDURANCE Character Root");
        m->root->setPosition({spawnPosition[0], spawnPosition[1], spawnPosition[2]});
        m->skeleton = m->pc->skinnedTransformManager->createSkinnedTransform(m->buffer, m->models, "ENDURANCE Character");
        if (!m->skeleton || m->models.size() != shapes.size()) { fail("Failed to create shared character skeleton."); return; }
        m->skeleton->setParent(m->root.get());
        const Transform normalized = m->skeleton->decomposeMatrix(m->normalization.meshToCharacter);
        m->skeleton->setLocalPosition(normalized.position);
        m->skeleton->setLocalRotation(normalized.rotation);
        m->skeleton->setLocalScale(normalized.scale);
        m->hair = std::make_shared<EnduranceCharacter::Pose>();
        if (!m->hair->bind(m->buffer->getSkeleton())) { fail("Cannot initialize the character's procedural pose."); return; }
        m->basePose = m->hair->relaxedPose();
        m->hairBones = m->hair->simulatedBoneCount();
        m->head = boneIndex(m->buffer->getSkeleton(), "J_Bip_C_Head");
        for (std::size_t i = 0; i < m->buffer->getSkeleton().bones.size(); ++i)
            if (m->buffer->getSkeleton().bones[i].name.find("J_Sec_Hair") != std::string::npos)
                m->hairIndices.push_back(i);
        m->pc->skinnedTransformManager->applyProceduralGlobalPose(m->skeleton, m->basePose);

        auto pipeline = [&](const char* fs, const char* vs, bool forward, const std::string& defines = "")
        {
            auto* p = m->pc->graphiquePipelineManager->createPipeline(resourcePath(fs), resourcePath(vs), false, true, forward, 2, defines);
            if (p) m->pipelines.push_back(p);
            if (!p || !p->isValid()) throw std::runtime_error(p ? p->getLastError() : "Cannot create character shader.");
            return p;
        };
        const char* jadeFs = "../Asset/Shader/Character/jade.fs.glsl";
        const char* skinnedForward = "../Asset/Shader/forward_transparency_skinned.vs.glsl";
        m->jade = pipeline(jadeFs, skinnedForward, true);
        m->face = pipeline(jadeFs, skinnedForward, true, "#define GE_CHARACTER_FACETS 0.0\n");
        m->eyes = pipeline(jadeFs, skinnedForward, true, "#define GE_CHARACTER_FACETS 0.0\n#define GE_CHARACTER_SCATTER 0.0\n");
        m->fabric = pipeline("../Asset/Shader/Character/fabric.fs.glsl", "../Asset/Shader/opaque_skinned.vs.glsl", false);
        m->cloth = pipeline("../Asset/Shader/Character/fabric.fs.glsl", "../Asset/Shader/opaque_cloth.vs.glsl", false);
        m->skinShadow = pipeline("../Asset/Shader/shadow.fs.glsl", "../Asset/Shader/opaque_skinned_shadow.vs.glsl", false);
        m->clothShadow = pipeline("../Asset/Shader/shadow.fs.glsl", "../Asset/Shader/opaque_cloth_shadow.vs.glsl", false);
        const auto directory = std::filesystem::path(path).parent_path();
        auto texture = [&](const std::string& filename) -> Ge::Textures*
        {
            if (filename.empty() || filename == "Shader_NoneNormal.png") return nullptr;
            const std::string full = (directory / filename).string();
            const auto found = m->textures.find(full);
            if (found != m->textures.end()) return found->second;
            if (!std::filesystem::exists(full))
            { m->warning += "Missing texture: " + filename + "\n"; return nullptr; }
            auto* t = m->pc->textureManager->createTexture(full.c_str(), true, true);
            if (t) m->textures.emplace(full, t);
            return t;
        };
        for (std::size_t i = 0; i < shapes.size(); ++i)
        {
            const Part part = m->parts[i];
            auto* mat = m->pc->materialManager->createMaterial();
            m->materials.push_back(mat);
            const MaterialFiles files = materialFiles(part, metadata[i].diffuseTexture, metadata[i].normalTexture);
            if (auto* t = texture(files.albedo)) mat->setAlbedoTexture(t);
            if (auto* t = texture(files.normal)) mat->setNormalTexture(t);
            mat->setColor(glm::vec4(1.0f));
            mat->setMetallic(0.0f);
            mat->setEmission(0.0f);
            mat->setNormal(isClothing(part) ? .55f : .35f);
            mat->setRoughness(isClothing(part) ? .85f : part == Part::Hair ? .34f : part == Part::Iris ? .18f : .36f);
            const bool detail = part == Part::Iris || part == Part::EyeWhite || part == Part::EyeHighlight || part == Part::FaceDetail || part == Part::Mouth;
            if (detail) mat->setNormal(0.0f);
            if (part == Part::FaceDetail) mat->setRoughness(.55f);
            mat->setPipeline(isClothing(part) ? m->fabric : detail ? m->eyes : part == Part::Skin ? m->face : m->jade);
            mat->setDepthWrite(true);
            m->pc->skinnedTransformManager->setupMaterials(m->skeleton, mat);
            mat->setShadowPipeline(m->skinShadow);
            // Tiny floating eye decals must not shadow the face or neighbouring eye layers.
            mat->setCastShadow(!detail);
            m->models[i]->setName(metadata[i].meshName + " [" + partName(part) + "]");
            m->models[i]->setImmutableShadowContent(false);
            m->models[i]->setMaterial(mat);
        }

        m->workers = std::make_unique<ThreadPool>(2);
        for (std::size_t i = 0; i < shapes.size(); ++i)
        {
            const Part part = m->parts[i];
            if (part != Part::Dress && part != Part::Belt) continue;
            ClothBuildSettings build;
            build.maxParticles = std::clamp(part == Part::Dress ? dressParticles : beltParticles, 64u, 2400u);
            const auto pins = attachmentMask(part, shapes[i]->getVertices(), m->normalization.meshToCharacter, m->attachments);
            auto mesh = Ge::SkinnedClothMesh::build(m->buffer, pins, build, {i});
            if (!mesh) { fail("Cannot build cloth attachments for " + metadata[i].meshName); return; }
            Impl::ClothPart c;
            c.shape = i; c.part = part; c.particles = mesh->topology()->particleCount();
            c.simulation = std::make_unique<Ge::SkinnedCloth>();
            ClothSettings settings;
            settings.density = part == Part::Dress ? .22f : .65f;
            // Retain the layered silhouette while allowing the hem to respond
            // to wind; the general cape default collapses this fitted dress.
            settings.bendCompliance = part == Part::Dress ? .00002f : .0005f;
            settings.collisionThickness = .008f / (std::max)(normalized.scale.x, .0001f);
            // Solver speed, like thickness, is in topology units before scale.
            settings.maxSpeed = 1000.0f / (std::max)(normalized.scale.x, .0001f);
            settings.selfCollision = part == Part::Dress;
            if (!c.simulation->bind(mesh, m->skeleton, settings)) { fail("Cannot bind character cloth."); return; }
            c.simulation->setAutomaticTeleportReset(false);
            if (!c.simulation->startAsync(m->workers.get())) { fail("Cannot schedule character cloth on CPU workers."); return; }
            m->clothes.push_back(std::move(c));
        }
        m->pivot = std::make_unique<Ge::Empty>();
        m->pivot->setName("ENDURANCE Camera Pivot");
        m->pivot->setParent(m->root.get());
        m->pivot->setLocalScale(glm::vec3(1.0f));
        m->camera = m->pc->cameraManager->createCamera("ENDURANCE Third Person", -100);
        m->camera->setParent(m->pivot.get());
        m->camera->setLocalRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m->camera->setLocalScale(glm::vec3(1.0f));
        m->camera->setNear(.04f);
        m->camera->setFar(100000.0f);
        m->camera->setFirstShadowCascadeDistance(16.0f);
        if (!m->pc->skinnedTransformManager->registerPostPoseCallback(m->skeleton, this,
            [this](float dt) { afterPose(dt); })) { fail("Cannot register character pose composition."); return; }
        m->registered = true;
        activeCharacter = this;
        scrollDelta = 0.0;
        previousScroll = glfwSetScrollCallback(m->pc->gdm->str_window, characterScroll);
        update();
        Ge::Debug::Info("ENDURANCE character: %u meshes, %u bones, %.3fm authored -> %.3fm, %u cloth particles",
            static_cast<unsigned>(meshCount()), static_cast<unsigned>(m->buffer->getSkeleton().bones.size()),
            importedBodyHeight(), height, static_cast<unsigned>(clothParticleCount()));
    }
    catch (const std::exception& e) { fail(e.what()); }
}

void EnduranceCharacterComponent::stop()
{
    if (!m) return;
    if (activeCharacter == this)
    {
        if (m->pc && m->pc->gdm && m->pc->gdm->str_window)
            glfwSetScrollCallback(m->pc->gdm->str_window, previousScroll);
        activeCharacter = nullptr; previousScroll = nullptr; scrollDelta = 0.0;
    }
    if (m->pc && m->skeleton)
        m->pc->skinnedTransformManager->unregisterPostPoseCallback(m->skeleton, this);
    // No worker may retain a solver, asset or scene object after destruction.
    m->clothes.clear();
    if (m->hairJob.valid()) m->hairJob.wait();
    m->workers.reset();
    if (m->pc)
    {
        if (m->camera) { m->camera->setParent(nullptr); m->pc->cameraManager->releaseCamera(m->camera); }
        for (auto* model : m->models)
        { model->setParent(nullptr); m->pc->modelManager->destroyModel(model); }
        if (m->skeleton)
        { m->skeleton->setParent(nullptr); m->pc->skinnedTransformManager->destroySkinnedTransform(m->skeleton); }
        for (auto* material : m->materials) m->pc->materialManager->destroyMaterial(material);
        if (m->buffer) m->pc->modelManager->destroyBuffer(m->buffer);
        for (const auto& texture : m->textures) m->pc->textureManager->destroyTexture(texture.second);
        for (auto* pipeline : m->pipelines) m->pc->graphiquePipelineManager->destroyPipeline(pipeline);
    }
    if (m->pivot) m->pivot->setParent(nullptr);
    m = std::make_unique<Impl>();
}

void EnduranceCharacterComponent::setRootPosition(const glm::vec3& position, bool teleport)
{
    if (!finite(position)) return;
    spawnPosition = {{position.x, position.y, position.z}};
    if (m->root) m->root->setPosition(position);
    if (teleport) resetSecondaryMotion();
}

bool EnduranceCharacterComponent::setBasePose(const std::vector<glm::mat4>& pose)
{
    if (!m->skeleton || pose.size() != m->basePose.size()) return false;
    for (const auto& matrix : pose)
    {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (!finite(matrix[c][r])) return false;
        const float determinant = glm::determinant(matrix);
        if (!finite(determinant) || std::abs(determinant) < 1.0e-10f) return false;
    }
    m->basePose = pose;
    return true;
}

void EnduranceCharacterComponent::resetSecondaryMotion()
{
    ++m->epoch;
    m->hairReset = true;
    m->lastHair.valid = false;
    for (auto& c : m->clothes) c.simulation->reset();
}

void EnduranceCharacterComponent::update()
{
    if (!m->registered)
    {
        if (enabled && m->error.empty()) start();
        return;
    }
    if (!enabled) { stop(); return; }
    const bool capture = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
    if (!capture && m->pc->inputManager->getMouse(GLFW_MOUSE_BUTTON_RIGHT))
    {
        cameraYaw -= static_cast<float>(m->pc->inputManager->axisMouseX()) * .18f;
        cameraPitch -= static_cast<float>(m->pc->inputManager->axisMouseY()) * .18f;
    }
    cameraDistance *= std::exp(static_cast<float>(-scrollDelta) * .12f);
    scrollDelta = 0.0;
    cameraDistance = safe(cameraDistance, 3.0f, .55f, 8.0f);
    cameraPitch = safe(cameraPitch, -8.0f, -75.0f, 65.0f);
    cameraYaw = finite(cameraYaw) ? std::remainder(cameraYaw, 360.0f) : 180.0f;
    const glm::vec3 pivotPosition(0.0f, safe(cameraTargetHeight, 1.05f, .2f, 3.0f), 0.0f);
    const glm::quat pivotRotation(glm::radians(glm::vec3(cameraPitch, cameraYaw, 0.0f)));
    const glm::vec3 cameraPosition(0.0f, 0.0f, cameraDistance);
    if (m->pivot->getLocalPosition() != pivotPosition) m->pivot->setLocalPosition(pivotPosition);
    if (m->pivot->getLocalRotation() != pivotRotation) m->pivot->setLocalRotation(pivotRotation);
    if (m->camera->getLocalPosition() != cameraPosition) m->camera->setLocalPosition(cameraPosition);
    const float fov = safe(cameraFov, 55.0f, 25.0f, 90.0f);
    if (m->camera->getFieldOfView() != fov) m->camera->setFieldOfView(fov);
    if (m->wasHairEnabled != hairPhysics || m->wasClothEnabled != clothPhysics)
    {
        resetSecondaryMotion();
        m->wasHairEnabled = hairPhysics;
        m->wasClothEnabled = clothPhysics;
    }
}

void EnduranceCharacterComponent::afterPose(float deltaSeconds)
{
    if (!m->registered) return;
    const float dt = safe(deltaSeconds, 0.0f, 0.0f, .10f);
    m->seconds += dt;
    m->elapsedHair = (std::min)(m->elapsedHair + dt, .10f);
    glm::vec3 air(wind[0], wind[1], wind[2]);
    if (useEnvironmentWind && EnduranceBiomeEffectsComponent::active())
        air = EnduranceBiomeEffectsComponent::active()->airVelocity();
    else
    {
        const glm::vec2 gust = EnduranceEnvironment::particleWind({air.x, air.z}, m->seconds, .4f, 18.0f);
        air.x = gust.x; air.z = gust.y;
    }
    if (!finite(air)) air = glm::vec3(0.0f);
    air *= safe(windStrength, 1.0f, 0.0f, 4.0f);
    const auto base = m->skeleton->isAnimationPlaying() ? m->skeleton->getGlobalPose() : m->basePose;
    if (m->hairJob.valid() && m->hairJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            auto result = m->hairJob.get();
            if (result.epoch == m->epoch && result.valid) m->lastHair = std::move(result);
        }
        catch (const std::exception& e) { m->warning = std::string("Hair worker: ") + e.what(); }
    }
    auto composed = base;
    if (hairPhysics && m->lastHair.valid && m->lastHair.pose.size() == base.size())
    {
        const glm::mat4 headCorrection = m->head >= 0
            ? base[m->head] * glm::inverse(m->lastHair.baseHead) : glm::mat4(1.0f);
        for (std::size_t bone : m->hairIndices) composed[bone] = headCorrection * m->lastHair.pose[bone];
    }
    m->pc->skinnedTransformManager->applyProceduralGlobalPose(m->skeleton, composed);
    if (hairPhysics && !m->hairJob.valid() && dt > 0.0f)
    {
        const auto solver = m->hair;
        const glm::mat4 world = m->skeleton->getModelMatrix();
        const std::uint64_t epoch = m->epoch;
        const bool reset = m->hairReset;
        const float elapsed = m->elapsedHair;
        const int head = m->head;
        auto task = std::make_shared<std::packaged_task<Impl::HairResult()>>(
            [solver, base, world, air, elapsed, reset, epoch, head]()
            {
                const auto begin = std::chrono::steady_clock::now();
                Impl::HairResult result;
                result.epoch = epoch;
                result.valid = solver->update(base, world, air, elapsed, reset);
                result.pose = solver->pose();
                result.baseHead = head >= 0 ? base[head] : glm::mat4(1.0f);
                result.lengthError = solver->maxLengthError();
                result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                return result;
            });
        m->hairJob = task->get_future();
        m->workers->enqueue(std::function<void()>([task]() { (*task)(); }));
        m->hairReset = false; m->elapsedHair = 0.0f;
    }
    const auto capsules = bodyCapsules(m->buffer->getSkeleton(), composed, m->skeleton->getModelMatrix(), m->normalization.heightMetres);
    for (auto& c : m->clothes)
    {
        c.simulation->setActive(clothPhysics);
        ClothWind windInput;
        windInput.velocity = air;
        windInput.drag = c.part == Part::Dress ? .65f : .30f;
        c.simulation->update(dt, capsules, false, {}, windInput);
        const bool drawCloth = clothPhysics && c.simulation->outputReady() && !c.simulation->workerFailed();
        if (drawCloth != c.drawingCloth)
        {
            auto* mat = m->materials[c.shape];
            m->pc->skinnedTransformManager->setupMaterials(m->skeleton, mat);
            mat->setPipeline(drawCloth ? m->cloth : m->fabric);
            mat->setShadowPipeline(drawCloth ? m->clothShadow : m->skinShadow);
            if (drawCloth) mat->getAditionalSSBO().push_back(c.simulation->getBufferID(0));
            m->models[c.shape]->setMaterial(mat);
            c.drawingCloth = drawCloth;
        }
        if (c.simulation->workerFailed()) m->warning = "A cloth worker failed; the affected garment uses its skinned pose.";
    }
}

void EnduranceCharacterComponent::onGUI()
{
    ImGui::TextUnformatted("ENDURANCE character");
    if (ImGui::Checkbox("Enabled##character", &enabled) && enabled) m->error.clear();
    ImGui::Checkbox("Hair physics", &hairPhysics);
    ImGui::SameLine(); ImGui::Checkbox("Cloth physics", &clothPhysics);
    ImGui::Checkbox("Use environment wind", &useEnvironmentWind);
    ImGui::DragFloat3("Fallback wind (m/s)", wind.data(), .1f, -50.0f, 50.0f);
    ImGui::SliderFloat("Wind strength", &windStrength, 0.0f, 4.0f);
    if (ImGui::DragFloat3("Character position", spawnPosition.data(), .1f))
        setRootPosition({spawnPosition[0], spawnPosition[1], spawnPosition[2]}, true);
    ImGui::DragFloat("Height on restart (m)", &height, .01f, .3f, 5.0f);
    ImGui::SliderFloat("Camera distance", &cameraDistance, .55f, 8.0f);
    ImGui::DragFloat("Camera yaw", &cameraYaw, 1.0f);
    ImGui::SliderFloat("Camera pitch", &cameraPitch, -75.0f, 65.0f);
    ImGui::SliderFloat("Camera target height", &cameraTargetHeight, .2f, 2.0f);
    ImGui::SliderFloat("Field of view", &cameraFov, 25.0f, 90.0f);
    ImGui::TextUnformatted("Right mouse: orbit. Mouse wheel: zoom.");
    if (m->registered)
    {
        ImGui::Text("Authored body %.3fm; target %.3fm; %u meshes", importedBodyHeight(), m->normalization.heightMetres, static_cast<unsigned>(meshCount()));
        ImGui::Text("Hair: %u bones, %.3f ms CPU; cloth: %u particles, %u ready parts", static_cast<unsigned>(hairBoneCount()), m->lastHair.milliseconds,
            static_cast<unsigned>(clothParticleCount()), static_cast<unsigned>(readyClothCount()));
        double clothMilliseconds = 0.0;
        for (const auto& c : m->clothes) clothMilliseconds += c.simulation->asyncDiagnostics().workerSeconds * 1000.0;
        ImGui::Text("Cloth CPU work (sum of latest jobs): %.3f ms", clothMilliseconds);
        ImGui::Text("Belt height %.3fm; dress release %.3f to %.3fm", m->attachments.waistHeight, m->attachments.dressPinnedHeight, m->attachments.dressFreeHeight);
        if (ImGui::Button("Reset secondary motion")) resetSecondaryMotion();
    }
    if (!m->error.empty()) ImGui::TextWrapped("%s", m->error.c_str());
    if (!m->warning.empty()) ImGui::TextWrapped("%s", m->warning.c_str());
}

std::string EnduranceCharacterComponent::serialize() { return JS::serializeStruct(*this); }
void EnduranceCharacterComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if (context.parseTo(*this) != JS::Error::NoError) m->error = "Invalid character settings.";
    height = safe(height, 1.7f, .3f, 5.0f);
    for (float& p : spawnPosition) if (!finite(p)) p = 0.0f;
    for (float& w : wind) w = safe(w, 0.0f, -100.0f, 100.0f);
    windStrength = safe(windStrength, 1.0f, 0.0f, 4.0f);
    waistHeight = safe(waistHeight, -1.0f, -1.0f, height);
    dressReleaseDistance = safe(dressReleaseDistance, .20f, .04f, .5f);
    dressParticles = std::clamp(dressParticles, 64u, 2400u);
    beltParticles = std::clamp(beltParticles, 64u, 2400u);
}
