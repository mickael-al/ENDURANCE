#include "EnduranceCharacterPose.hpp"
#include "ShapeBufferSkeleton.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace
{
bool finite(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}
bool finite(const glm::vec3& value)
{ return finite(value.x) && finite(value.y) && finite(value.z); }
bool finite(const glm::mat4& value)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) if (!finite(value[c][r])) return false;
    return true;
}
float bounded(float value, float fallback, float lo, float hi)
{ return finite(value) ? std::clamp(value, lo, hi) : fallback; }
glm::vec3 boundedLength(glm::vec3 value, float maximum)
{
    const float length = glm::length(value);
    if (!finite(value) || !finite(length)) return glm::vec3(0.0f);
    return length > maximum && length > 1e-8f ? value * (maximum / length) : value;
}
glm::vec3 safeDirection(glm::vec3 value, glm::vec3 fallback)
{
    const float squared = glm::dot(value, value);
    return finite(value) && squared > 1e-12f ? value / std::sqrt(squared) : fallback;
}
glm::mat4 interpolate(const glm::mat4& first, const glm::mat4& second, float alpha)
{
    if (alpha <= 0.0f) return first;
    if (alpha >= 1.0f) return second;
    glm::vec3 scaleA, scaleB;
    glm::mat3 axesA(first), axesB(second);
    for (int axis = 0; axis < 3; ++axis)
    {
        scaleA[axis] = glm::length(axesA[axis]);
        scaleB[axis] = glm::length(axesB[axis]);
        if (scaleA[axis] < 1e-8f || scaleB[axis] < 1e-8f) return second;
        axesA[axis] /= scaleA[axis]; axesB[axis] /= scaleB[axis];
    }
    if (glm::determinant(axesA) < 0.0f) { axesA[0] = -axesA[0]; scaleA.x = -scaleA.x; }
    if (glm::determinant(axesB) < 0.0f) { axesB[0] = -axesB[0]; scaleB.x = -scaleB.x; }
    const glm::quat rotation = glm::normalize(glm::slerp(glm::normalize(glm::quat_cast(axesA)),
        glm::normalize(glm::quat_cast(axesB)), alpha));
    return glm::translate(glm::mat4(1.0f), glm::mix(glm::vec3(first[3]), glm::vec3(second[3]), alpha)) *
        glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), glm::mix(scaleA, scaleB, alpha));
}
glm::vec3 limitCone(glm::vec3 direction, const glm::vec3& rest, float angle)
{
    direction = safeDirection(direction, rest);
    const float dot = glm::clamp(glm::dot(direction, rest), -1.0f, 1.0f);
    if (dot >= std::cos(angle)) return direction;
    glm::vec3 tangent = direction - dot * rest;
    if (glm::dot(tangent, tangent) < 1e-10f)
        tangent = glm::cross(rest, std::abs(rest.x) < 0.8f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1));
    return std::cos(angle) * rest + std::sin(angle) * glm::normalize(tangent);
}
std::string shortName(const std::string& name)
{
    const auto split = name.find_last_of(":|");
    return name.substr(split == std::string::npos ? 0 : split + 1);
}
}

namespace EnduranceCharacter
{
bool Pose::bind(const Skeleton& skeleton)
{
    *this = Pose{};
    const std::size_t count = skeleton.bones.size();
    if (count == 0 || !finite(skeleton.inverseBindTransform)) return false;
    m_inverseBind = skeleton.inverseBindTransform;
    m_relaxed.reserve(count); m_parents.reserve(count);
    for (const auto& bone : skeleton.bones)
    {
        if (!finite(bone.bindPose) || std::abs(glm::determinant(bone.bindPose)) < 1e-12f) return false;
        m_relaxed.push_back(bone.bindPose); m_parents.push_back(bone.parent);
    }
    std::vector<unsigned char> visited(count, 0);
    std::function<bool(std::size_t)> visit = [&](std::size_t index)
    {
        if (visited[index] == 2) return true;
        if (visited[index] == 1) return false;
        visited[index] = 1;
        const int parent = m_parents[index];
        if (parent >= 0 && (static_cast<std::size_t>(parent) >= count || !visit(parent))) return false;
        visited[index] = 2; m_order.push_back(index); return true;
    };
    for (std::size_t i = 0; i < count; ++i) if (!visit(i)) { *this = Pose{}; return false; }
    const auto find = [&](const char* name)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (shortName(skeleton.bones[i].name) == name) return static_cast<int>(i);
        return -1;
    };
    m_head = find("J_Bip_C_Head"); m_neck = find("J_Bip_C_Neck");
    m_chest = find("J_Bip_C_UpperChest"); m_hips = find("J_Bip_C_Hips");
    const auto below = [&](std::size_t bone, int ancestor)
    {
        for (int index = static_cast<int>(bone); index >= 0; index = m_parents[index])
            if (index == ancestor) return true;
        return false;
    };
    // A standing base pose, independent of animation clips. Rotations affect
    // each complete subtree, so finger and clothing skinning remain coherent.
    for (int side = 0; side < 2; ++side)
    {
        const int upper = find(side == 0 ? "J_Bip_L_UpperArm" : "J_Bip_R_UpperArm");
        const int lower = find(side == 0 ? "J_Bip_L_LowerArm" : "J_Bip_R_LowerArm");
        const int hand = find(side == 0 ? "J_Bip_L_Hand" : "J_Bip_R_Hand");
        if (upper < 0 || lower < 0 || hand < 0 || !below(lower, upper) || !below(hand, lower)) continue;
        const auto turn = [&](int bone, int child, glm::vec3 desired)
        {
            const glm::vec3 pivot(m_relaxed[bone][3]);
            const glm::vec3 original = safeDirection(glm::vec3(m_relaxed[child][3]) - pivot, desired);
            const glm::mat4 correction = glm::translate(glm::mat4(1.0f), pivot) *
                glm::mat4_cast(glm::rotation(original, glm::normalize(desired))) *
                glm::translate(glm::mat4(1.0f), -pivot);
            for (std::size_t i = 0; i < count; ++i) if (below(i, bone)) m_relaxed[i] = correction * m_relaxed[i];
        };
        // This game's exported skeleton is source Y-up. Slight elbow spacing
        // leaves room for the loose dress, with a small forward forearm bend.
        turn(upper, lower, glm::vec3(side == 0 ? 0.28f : -0.28f, -1.0f, 0.03f));
        turn(lower, hand, glm::vec3(side == 0 ? 0.18f : -0.18f, -1.0f, 0.14f));
    }
    m_jointByBone.assign(count, -1);
    for (std::size_t bone : m_order)
    {
        const std::string name = shortName(skeleton.bones[bone].name);
        if (name.find("J_Sec_Hair") != 0 || name.find("_end") != std::string::npos) continue;
        int child = -1;
        for (std::size_t i = 0; i < count; ++i)
            if (m_parents[i] == static_cast<int>(bone) && shortName(skeleton.bones[i].name).find("J_Sec_Hair") == 0)
            { child = static_cast<int>(i); break; }
        if (child < 0) continue; // No guessed orientation for unidentified tips.
        Joint joint;
        joint.bone = bone;
        joint.localTip = glm::vec3(glm::inverse(skeleton.bones[bone].bindPose) * skeleton.bones[child].bindPose[3]);
        if (glm::dot(joint.localTip, joint.localTip) < 1e-12f) continue;
        for (int parent = m_parents[bone]; parent >= 0 && m_jointByBone[parent] >= 0; parent = m_parents[parent])
            ++joint.depth;
        m_jointByBone[bone] = static_cast<int>(m_joints.size());
        m_joints.push_back(joint);
    }
    m_pose = m_relaxed;
    return true;
}

void Pose::reset()
{
    m_initialized = false; m_accumulator = 0.0f;
    m_lastSubsteps = 0; m_maxLengthError = 0.0f;
    for (auto& joint : m_joints) joint.velocity = glm::vec3(0.0f);
}

std::vector<ClothCapsule> Pose::bodyCapsules(const std::vector<glm::mat4>& base,
    const glm::mat4& skeletonToWorld) const
{
    std::vector<ClothCapsule> result;
    if (m_head < 0 || m_hips < 0) return result;
    const auto position = [&](int bone) { return glm::vec3(skeletonToWorld * base[bone][3]); };
    const glm::vec3 head = position(m_head), hips = position(m_hips);
    const float torso = glm::length(head - hips);
    if (!finite(torso) || torso < 0.05f) return result;
    const glm::vec3 up = safeDirection(head - hips, glm::vec3(0, 1, 0));
    const glm::vec3 center = head + up * (0.20f * torso);
    result.push_back({center, center, 0.155f * torso, 0.0f, ClothContactResponse::SeparationOnly});
    if (m_neck >= 0)
    {
        result.push_back({position(m_neck), head, 0.065f * torso, 0.0f, ClothContactResponse::SeparationOnly});
        if (m_chest >= 0)
            result.push_back({position(m_chest), position(m_neck), 0.17f * torso, 0.0f, ClothContactResponse::SeparationOnly});
    }
    return result;
}

bool Pose::buildPose(const std::vector<glm::mat4>& localPose, const glm::mat4& skeletonToWorld,
    const glm::vec3& windWorld, float step, bool initialize, const std::vector<ClothCapsule>& capsules)
{
    const glm::mat4 worldToSkeleton = glm::inverse(skeletonToWorld);
    const float stiffness = bounded(settings.stiffness, 95.0f, 0.0f, 800.0f);
    const float damping = bounded(settings.damping, 10.0f, 0.0f, 60.0f);
    const float drag = bounded(settings.airDrag, 2.5f, 0.0f, 20.0f);
    const float angle = glm::radians(bounded(settings.maxAngleDegrees, 38.0f, 1.0f, 80.0f));
    const float speed = bounded(settings.maxRelativeSpeed, 8.0f, 0.1f, 50.0f);
    const float radius = bounded(settings.collisionRadius, 0.006f, 0.0f, 0.05f);
    const glm::vec3 gravity(0, -9.81f * bounded(settings.gravityScale, 0.25f, 0.0f, 2.0f), 0);
    m_maxLengthError = 0.0f;
    for (std::size_t bone : m_order)
    {
        const int parent = m_parents[bone];
        m_pose[bone] = parent >= 0 ? m_pose[parent] * localPose[bone] : localPose[bone];
        const int jointIndex = m_jointByBone[bone];
        if (jointIndex < 0) continue;
        Joint& joint = m_joints[jointIndex];
        const glm::mat4 boneWorld = skeletonToWorld * m_pose[bone];
        const glm::vec3 root(boneWorld[3]);
        const glm::vec3 target(boneWorld * glm::vec4(joint.localTip, 1));
        const float length = glm::length(target - root);
        if (!finite(length) || length < 1e-7f) return false;
        const glm::vec3 rest = (target - root) / length;
        if (initialize)
        {
            joint.tip = target; joint.previousTarget = target; joint.velocity = glm::vec3(0);
            continue;
        }
        const glm::vec3 oldTip = joint.tip;
        glm::vec3 targetVelocity(0);
        if (step > 0.0f)
        {
            targetVelocity = (target - joint.previousTarget) / step;
            // Implicit spring/damper coefficients stay stable even at a stiff
            // root. Damping acts relative to the moving attachment, not zero
            // world velocity; a fast moving actor is not artificially capped.
            const float rootStiffness = stiffness * (joint.depth == 0 ? 2.2f : 1.0f);
            const glm::vec3 acceleration = rootStiffness * (target - joint.tip) + gravity +
                drag * (windWorld - joint.velocity);
            const float divisor = 1.0f + damping * step + rootStiffness * step * step;
            joint.velocity = (joint.velocity + step * (acceleration + damping * targetVelocity)) / divisor;
            joint.velocity = targetVelocity + boundedLength(joint.velocity - targetVelocity, speed);
            joint.tip += step * joint.velocity;
        }
        const float jointAngle = angle * (joint.depth == 0 ? 0.45f : 1.0f);
        // A few cheap constraint projections preserve bone length and avoid the
        // head. The final projection is always on the exact-length cone.
        for (int pass = 0; pass < 3; ++pass)
        {
            joint.tip = root + length * limitCone(joint.tip - root, rest, jointAngle);
            if (!capsules.empty()) joint.tip = projectOutsideClothCapsules(joint.tip, target, capsules, radius);
        }
        const glm::vec3 direction = limitCone(joint.tip - root, rest, jointAngle);
        joint.tip = root + length * direction;
        if (step > 0.0f)
        {
            joint.velocity = targetVelocity + boundedLength((joint.tip - oldTip) / step - targetVelocity, speed);
            joint.previousTarget = target;
        }
        m_maxLengthError = (std::max)(m_maxLengthError, std::abs(glm::length(joint.tip - root) - length));
        const glm::mat4 correction = worldToSkeleton * glm::translate(glm::mat4(1), root) *
            glm::mat4_cast(glm::rotation(rest, direction)) * glm::translate(glm::mat4(1), -root) * skeletonToWorld;
        m_pose[bone] = correction * m_pose[bone];
        if (!finite(m_pose[bone])) return false;
    }
    return true;
}

bool Pose::update(const std::vector<glm::mat4>& baseGlobalPose, const glm::mat4& actorModelMatrix,
    const glm::vec3& windWorld, float deltaSeconds, bool forceReset,
    const std::vector<ClothCapsule>& worldCapsules)
{
    m_lastSubsteps = 0;
    if (m_parents.empty() || baseGlobalPose.size() != m_parents.size() || !finite(actorModelMatrix)) return false;
    const glm::mat4 skeletonToWorld = actorModelMatrix * m_inverseBind;
    const float determinant = glm::determinant(skeletonToWorld);
    if (!finite(determinant) || std::abs(determinant) < 1e-12f) return false;
    std::vector<glm::mat4> local(baseGlobalPose.size());
    for (std::size_t bone : m_order)
    {
        if (!finite(baseGlobalPose[bone]) || std::abs(glm::determinant(baseGlobalPose[bone])) < 1e-12f) return false;
        local[bone] = m_parents[bone] < 0 ? baseGlobalPose[bone]
            : glm::inverse(baseGlobalPose[m_parents[bone]]) * baseGlobalPose[bone];
    }
    if (!settings.enabled)
    {
        m_pose = baseGlobalPose; reset(); return true;
    }
    const std::vector<ClothCapsule> capsules = worldCapsules.empty()
        ? bodyCapsules(baseGlobalPose, skeletonToWorld) : worldCapsules;
    const glm::vec3 wind = finite(windWorld) ? boundedLength(windWorld, 300.0f) : glm::vec3(0);
    if (!m_initialized || forceReset)
    {
        m_pose.resize(local.size());
        if (!buildPose(local, skeletonToWorld, wind, 0.0f, true, capsules)) return false;
        m_previousLocal = local; m_previousSkeletonToWorld = skeletonToWorld;
        m_accumulator = 0.0f; m_initialized = true;
        return true;
    }
    const float step = bounded(settings.fixedSeconds, 1.0f / 120.0f, 1.0f / 240.0f, 1.0f / 60.0f);
    const unsigned limit = std::clamp(settings.maxSubsteps, 1u, 16u);
    const float dt = bounded(deltaSeconds, 0.0f, 0.0f, step * limit);
    m_accumulator = (std::min)(m_accumulator + dt, step * limit);
    std::vector<glm::mat4> interpolated(local.size());
    while (m_accumulator + 1e-7f >= step && m_lastSubsteps < limit)
    {
        m_accumulator = (std::max)(0.0f, m_accumulator - step);
        const float alpha = dt > 0.0f ? glm::clamp(1.0f - m_accumulator / dt, 0.0f, 1.0f) : 1.0f;
        for (std::size_t i = 0; i < local.size(); ++i) interpolated[i] = interpolate(m_previousLocal[i], local[i], alpha);
        const glm::mat4 frame = interpolate(m_previousSkeletonToWorld, skeletonToWorld, alpha);
        if (!buildPose(interpolated, frame, wind, step, false, capsules)) { reset(); return false; }
        ++m_lastSubsteps;
    }
    // Publish at the current base pose even between simulation ticks. This
    // keeps attachments on the head without forcing another simulation step.
    const bool valid = buildPose(local, skeletonToWorld, wind, 0.0f, false, capsules);
    m_previousLocal = std::move(local); m_previousSkeletonToWorld = skeletonToWorld;
    if (!valid) reset();
    return valid;
}
}
