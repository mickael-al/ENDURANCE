#pragma once

#include "Behaviour.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#ifndef JS_STL_ARRAY
#define JS_STL_ARRAY
#endif
#include "json_struct.h"

// Game policy only: shared engine skeleton, materials, cloth and worker pool.
// No character controller or terrain collision is required for this first stage.
class EnduranceCharacterComponent final : public Behaviour
{
public:
    EnduranceCharacterComponent();
    ~EnduranceCharacterComponent() override;
    void start() override;
    void stop() override;
    void update() override;
    void fixedUpdate() override {}
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    // Render-thread entry points for future locomotion and IK. A true teleport
    // explicitly resets secondary motion; ordinary rapid movement does not.
    void setRootPosition(const glm::vec3& position, bool teleport = false);
    bool setBasePose(const std::vector<glm::mat4>& sourceGlobalPose);
    void resetSecondaryMotion();
    bool ready() const;
    const std::string& error() const;
    std::size_t meshCount() const;
    std::size_t clothParticleCount() const;
    std::size_t hairBoneCount() const;
    std::size_t readyClothCount() const;
    float importedBodyHeight() const;

    bool enabled = true, hairPhysics = true, clothPhysics = true;
    bool useEnvironmentWind = true;
    std::string modelFile = "../ENDURANCE/Ressources/Model/Character/Endurance.fbx";
    float height = 1.70f;
    std::array<float, 3> spawnPosition{{0.0f, 0.0f, 0.0f}};
    float cameraDistance = 3.0f, cameraYaw = 180.0f, cameraPitch = -8.0f;
    float cameraTargetHeight = 1.05f, cameraFov = 55.0f;
    std::array<float, 3> wind{{3.0f, 0.0f, 1.0f}};
    float windStrength = 1.0f;
    float waistHeight = -1.0f;
    float dressReleaseDistance = 0.20f;
    unsigned dressParticles = 720, beltParticles = 240;
private:
    void afterPose(float deltaSeconds);
    struct Impl;
    std::unique_ptr<Impl> m;
};
