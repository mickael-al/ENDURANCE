#pragma once

#include "Physics/ClothSimulation.hpp"
#include <cstddef>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Skeleton;

namespace EnduranceCharacter
{
struct HairSettings
{
    bool enabled = true;
    float fixedSeconds = 1.0f / 120.0f;
    unsigned maxSubsteps = 8;
    float stiffness = 95.0f;       // Spring acceleration per metre of deflection.
    float damping = 10.0f;         // Relative to the moving authored hair pose.
    float airDrag = 2.5f;          // Relative air velocity, in inverse seconds.
    float gravityScale = 0.25f;
    float maxAngleDegrees = 38.0f;
    float maxRelativeSpeed = 8.0f; // Does not cap the character's world speed.
    float collisionRadius = 0.006f;
};

// Instance-owned CPU state. The caller schedules update() on its existing task
// pool and publishes pose() only after completion. No GL, scene or thread access.
// Both input and output are global skeleton matrices, in the FBX source space.
class Pose
{
public:
    bool bind(const Skeleton& skeleton);
    const std::vector<glm::mat4>& relaxedPose() const { return m_relaxed; }
    const std::vector<glm::mat4>& pose() const { return m_pose; }
    bool update(const std::vector<glm::mat4>& baseGlobalPose,
        const glm::mat4& actorModelMatrix, const glm::vec3& windWorld,
        float deltaSeconds, bool forceReset = false,
        const std::vector<ClothCapsule>& worldCapsules = {});
    void reset();
    std::size_t simulatedBoneCount() const { return m_joints.size(); }
    float maxLengthError() const { return m_maxLengthError; }
    unsigned lastSubsteps() const { return m_lastSubsteps; }

    HairSettings settings;

private:
    struct Joint
    {
        std::size_t bone = 0;
        glm::vec3 localTip{};
        glm::vec3 tip{}, velocity{}, previousTarget{};
        unsigned depth = 0;
    };
    bool buildPose(const std::vector<glm::mat4>& localPose,
        const glm::mat4& skeletonToWorld, const glm::vec3& windWorld,
        float step, bool initialize, const std::vector<ClothCapsule>& capsules);
    std::vector<ClothCapsule> bodyCapsules(const std::vector<glm::mat4>& base,
        const glm::mat4& skeletonToWorld) const;

    std::vector<int> m_parents, m_jointByBone;
    std::vector<std::size_t> m_order;
    std::vector<Joint> m_joints;
    std::vector<glm::mat4> m_relaxed, m_pose, m_previousLocal;
    glm::mat4 m_inverseBind{1.0f}, m_previousSkeletonToWorld{1.0f};
    int m_head = -1, m_neck = -1, m_chest = -1, m_hips = -1;
    float m_accumulator = 0.0f, m_maxLengthError = 0.0f;
    unsigned m_lastSubsteps = 0;
    bool m_initialized = false;
};
}
