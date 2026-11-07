#pragma once

#include "ProceduralIK.hpp"
#include <array>
#include <cctype>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace Ge
{
struct ArmIKTarget
{
    glm::vec3 position{0.0f}; // World wrist position.
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f}; // X towards thumb, Y towards fingers.
    glm::vec3 poleDirection{0.0f, -1.0f, 0.0f}; // Relative to the shoulder, in world axes.
    bool enabled = false;
};

// Instance-owned post-animation correction. No model, physics or GL calls on
// animation workers; the shared skeleton/animation asset is never modified.
class SkeletalArmIK
{
public:
    static bool finite(glm::vec3 p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    static bool rotationOf(const glm::mat4& matrix, glm::quat& result)
    {
        glm::mat3 axes(matrix);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float length = glm::length(axes[axis]);
            if (!finite(axes[axis]) || length < 1e-8f) return false;
            axes[axis] /= length;
        }
        if (glm::determinant(axes) < 0.5f) return false;
        result = glm::normalize(glm::quat_cast(axes));
        return std::isfinite(result.w) && finite(glm::vec3(result.x, result.y, result.z));
    }

    template<class SkeletonType> void bind(const SkeletonType& skeleton)
    {
        m_chains = {};
        m_bindPose.clear();
        for (const auto& bone : skeleton.bones) m_bindPose.push_back(bone.bindPose);
        const auto compact = [](const std::string& name)
        {
            const auto start = name.find_last_of(":|");
            std::string result;
            for (unsigned char c : name.substr(start == std::string::npos ? 0 : start + 1))
                if (std::isalnum(c)) result += static_cast<char>(std::tolower(c));
            return result;
        };
        const auto find = [&](const std::string& name)
        {
            int suffixMatch = -1;
            bool ambiguous = false;
            for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
            {
                const std::string candidate = compact(skeleton.bones[i].name);
                if (candidate == name) return static_cast<int>(i);
                if (candidate.size() > name.size() &&
                    candidate.compare(candidate.size() - name.size(), name.size(), name) == 0)
                {
                    ambiguous = suffixMatch >= 0;
                    suffixMatch = static_cast<int>(i);
                }
            }
            return ambiguous ? -1 : suffixMatch;
        };
        const auto below = [&](int bone, int ancestor)
        {
            for (std::size_t guard = 0; guard < skeleton.bones.size(); ++guard)
            {
                if (bone < 0 || static_cast<std::size_t>(bone) >= skeleton.bones.size()) return false;
                if (bone == ancestor) return true;
                bone = skeleton.bones[bone].parent;
            }
            return false;
        };
        for (std::size_t side = 0; side < 2; ++side)
        {
            Chain& chain = m_chains[side];
            const std::string prefix = side == 0 ? "right" : "left";
            chain.joints = {{find(prefix + "arm"), find(prefix + "forearm"), find(prefix + "hand")}};
            if (chain.joints[0] < 0 || chain.joints[1] < 0 || chain.joints[2] < 0 ||
                !below(chain.joints[1], chain.joints[0]) || !below(chain.joints[2], chain.joints[1])) continue;
            glm::quat handRotation;
            const glm::mat4& hand = m_bindPose[chain.joints[2]];
            if (!rotationOf(hand, handRotation)) continue;
            glm::vec3 finger = glm::vec3(hand[1]);
            const int index = find(prefix + "handindex1");
            const int middle = find(prefix + "handmiddle1");
            if (middle >= 0 || index >= 0)
                finger = glm::vec3(m_bindPose[middle >= 0 ? middle : index][3] - hand[3]);
            if (glm::length2(finger) < 1e-10f) continue;
            finger = glm::normalize(finger);
            // Simplified Mixamo hands mirror their local X axis. Without the
            // other knuckles, using +X on both sides flips the left palm over.
            glm::vec3 thumb = glm::vec3(hand[0]) * (side == 0 ? 1.0f : -1.0f);
            const int thumbBone = find(prefix + "handthumb1");
            const int ring = find(prefix + "handring1");
            const int pinky = find(prefix + "handpinky1");
            if (index >= 0 && (pinky >= 0 || ring >= 0))
                thumb = glm::vec3(m_bindPose[index][3] - m_bindPose[pinky >= 0 ? pinky : ring][3]);
            else if (thumbBone >= 0)
                thumb = glm::vec3(m_bindPose[thumbBone][3] - hand[3]);
            thumb -= finger * glm::dot(thumb, finger);
            if (glm::length2(thumb) < 1e-10f) continue;
            thumb = glm::normalize(thumb);
            const glm::quat palm = glm::quat_cast(glm::mat3(thumb, finger, glm::cross(thumb, finger)));
            chain.gripToHand = glm::normalize(glm::inverse(palm) * handRotation);
            for (int joint = 0; joint < 3; ++joint)
                for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone)
                    if (below(static_cast<int>(bone), chain.joints[joint]))
                        chain.descendants[joint].push_back(bone);
            chain.valid = true;
        }
    }

    bool reference(std::size_t side, const std::vector<glm::mat4>& pose,
        const glm::mat4& skeletonToWorld, glm::vec3& shoulder, float& reach) const
    {
        if (side >= 2 || !m_chains[side].valid) return false;
        const auto& source = pose.size() == m_bindPose.size() ? pose : m_bindPose;
        const auto& joints = m_chains[side].joints;
        const glm::vec3 a(skeletonToWorld * source[joints[0]][3]);
        const glm::vec3 b(skeletonToWorld * source[joints[1]][3]);
        const glm::vec3 c(skeletonToWorld * source[joints[2]][3]);
        reach = glm::length(b - a) + glm::length(c - b);
        shoulder = a;
        return finite(a) && std::isfinite(reach) && reach > 0.01f;
    }

    void setTarget(std::size_t side, const ArmIKTarget& target, const glm::mat4& skeletonToWorld)
    {
        if (side >= 2 || !m_chains[side].valid) return;
        Chain& chain = m_chains[side];
        chain.enabled = false;
        if (!target.enabled || !finite(target.position) || !finite(target.poleDirection) ||
            !std::isfinite(target.orientation.w) || !finite(glm::vec3(target.orientation.x,
                target.orientation.y, target.orientation.z)) || glm::length2(target.orientation) < 1e-8f)
            return;
        const float determinant = glm::determinant(skeletonToWorld);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) return;
        const glm::mat4 inverse = glm::inverse(skeletonToWorld);
        // Keep disabled targets body-relative while blending out, so a moving
        // character never leaves an arm reaching for an old world position.
        chain.target = inverse * glm::translate(glm::mat4(1.0f), target.position) *
            glm::toMat4(glm::normalize(target.orientation));
        chain.pole = glm::vec3(inverse * glm::vec4(target.poleDirection, 0.0f));
        chain.enabled = true;
    }

    void clear(bool immediate = false)
    {
        for (Chain& chain : m_chains)
        {
            chain.enabled = false;
            if (immediate) chain.weight = 0.0f;
        }
    }
    bool needsEvaluation() const
    {
        for (const Chain& chain : m_chains)
            if (chain.enabled || chain.weight > 0.0f) return true;
        return false;
    }

    void apply(std::vector<glm::mat4>& pose, const glm::mat4& skeletonToWorld, float deltaTime)
    {
        if (!needsEvaluation() || pose.size() != m_bindPose.size()) return;
        const float determinant = glm::determinant(skeletonToWorld);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) return;
        const glm::mat4 worldToSkeleton = glm::inverse(skeletonToWorld);
        const float step = std::isfinite(deltaTime) ? glm::clamp(deltaTime, 0.0f, 0.1f) / 0.12f : 0.0f;
        for (Chain& chain : m_chains)
        {
            chain.weight = glm::clamp(chain.weight + (chain.enabled ? step : -step), 0.0f, 1.0f);
            if (!chain.valid || chain.weight <= 0.0f) continue;
            const auto position = [&](int joint)
            {
                return glm::vec3(skeletonToWorld * pose[chain.joints[joint]][3]);
            };
            const auto rotate = [&](int joint, const glm::quat& rotation)
            {
                const glm::vec3 pivot = position(joint);
                const glm::mat4 correction = worldToSkeleton *
                    glm::translate(glm::mat4(1.0f), pivot) * glm::toMat4(rotation) *
                    glm::translate(glm::mat4(1.0f), -pivot) * skeletonToWorld;
                for (std::size_t bone : chain.descendants[joint]) pose[bone] = correction * pose[bone];
            };
            const glm::mat4 target = skeletonToWorld * chain.target;
            glm::quat gripRotation;
            if (!rotationOf(target, gripRotation)) continue;
            const std::array<glm::vec3, 3> joints{{position(0), position(1), position(2)}};
            std::array<glm::vec3, 3> solved;
            if (!ProceduralIK::SolveTwoBone(joints, glm::vec3(target[3]),
                joints[0] + glm::vec3(skeletonToWorld * glm::vec4(chain.pole, 0.0f)), solved)) continue;
            const auto towards = [&](glm::vec3 from, glm::vec3 to)
            {
                if (glm::length2(from) < 1e-12f || glm::length2(to) < 1e-12f)
                    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                const glm::quat correction = glm::rotation(glm::normalize(from), glm::normalize(to));
                return glm::normalize(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), correction, chain.weight));
            };
            rotate(0, towards(joints[1] - joints[0], solved[1] - solved[0]));
            rotate(1, towards(position(2) - position(1), solved[2] - position(1)));
            glm::quat currentHand;
            if (rotationOf(skeletonToWorld * pose[chain.joints[2]], currentHand))
            {
                const glm::quat desired = glm::normalize(gripRotation * chain.gripToHand);
                const glm::quat blended = glm::normalize(glm::slerp(currentHand, desired, chain.weight));
                rotate(2, blended * glm::inverse(currentHand));
            }
        }
    }

private:
    struct Chain
    {
        std::array<int, 3> joints{{-1, -1, -1}};
        std::array<std::vector<std::size_t>, 3> descendants;
        glm::quat gripToHand{1.0f, 0.0f, 0.0f, 0.0f};
        glm::mat4 target{1.0f};
        glm::vec3 pole{0.0f, -1.0f, 0.0f};
        float weight = 0.0f;
        bool valid = false;
        bool enabled = false;
    };
    std::array<Chain, 2> m_chains;
    std::vector<glm::mat4> m_bindPose;
};
}
