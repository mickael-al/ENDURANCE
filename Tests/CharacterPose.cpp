#include "ProjectUser/EnduranceCharacterPose.hpp"
#include "ShapeBufferSkeleton.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-4f)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(a[column][row] - b[column][row]) > tolerance) return false;
    return true;
}
bool finite(const glm::mat4& matrix)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
Skeleton skeleton()
{
    Skeleton result;
    // Match the real FBX's source centimetres and rotated/scaled common mesh
    // frame. Hair dynamics must run in metres regardless of these import units.
    result.bindTransform = glm::translate(glm::mat4(1), glm::vec3(-1, 92, 1)) *
        glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1), glm::vec3(44));
    result.inverseBindTransform = glm::inverse(result.bindTransform);
    const auto add = [&](const char* name, int parent, glm::vec3 position)
    {
        Bone bone;
        bone.name = name; bone.parent = static_cast<std::int16_t>(parent);
        bone.bindPose = glm::translate(glm::mat4(1), position);
        bone.inverseBindPose = glm::inverse(bone.bindPose);
        result.bones.push_back(bone);
    };
    add("J_Bip_C_Hips", -1, {0, 90, 0});
    add("J_Bip_C_UpperChest", 0, {0, 120, 0});
    add("J_Bip_C_Neck", 1, {0, 132, 0});
    add("J_Bip_C_Head", 2, {0, 140, 0});
    add("J_Sec_Hair1_01", 3, {10, 155, -12});
    add("J_Sec_Hair2_01", 4, {10, 143, -12});
    add("J_Sec_Hair2_01_end", 5, {10, 132, -12});
    add("J_Bip_L_UpperArm", 1, {10, 128, 0});
    add("J_Bip_L_LowerArm", 7, {32, 128, 0});
    add("J_Bip_L_Hand", 8, {53, 128, 0});
    add("J_Bip_R_UpperArm", 1, {-10, 128, 0});
    add("J_Bip_R_LowerArm", 10, {-32, 128, 0});
    add("J_Bip_R_Hand", 11, {-53, 128, 0});
    return result;
}
glm::mat4 actor(const Skeleton& skeleton, glm::vec3 offset = glm::vec3(0))
{
    return glm::translate(glm::mat4(1), offset) *
        glm::scale(glm::mat4(1), glm::vec3(0.01f)) * skeleton.bindTransform;
}
EnduranceCharacter::Pose simulate(const Skeleton& skeleton, float dt, glm::vec3 wind)
{
    EnduranceCharacter::Pose pose;
    require(pose.bind(skeleton), "bind synthetic rig");
    const auto base = pose.relaxedPose();
    require(pose.update(base, actor(skeleton), wind, 0), "initialize without a clip");
    for (int frame = 0; frame < static_cast<int>(std::round(3.0f / dt)); ++frame)
        require(pose.update(base, actor(skeleton), wind, dt), "simulate fixed-step wind");
    return pose;
}
}

int main()
{
    const Skeleton rig = skeleton();
    EnduranceCharacter::Pose pose;
    require(pose.bind(rig), "bind");
    require(pose.simulatedBoneCount() == 2, "end bones are inherited rather than simulated twice");
    const auto base = pose.relaxedPose();
    require(base[9][3].y < base[8][3].y && base[8][3].y < base[7][3].y,
        "left arm relaxed from T-pose");
    require(base[12][3].y < base[11][3].y && base[11][3].y < base[10][3].y,
        "right arm relaxed from T-pose");
    require(near(rig.bones[9].bindPose, glm::translate(glm::mat4(1), glm::vec3(53, 128, 0))),
        "shared bind pose stays unchanged");
    const auto sixty = simulate(rig, 1.0f / 60.0f, {5, 0, 0});
    const auto oneTwenty = simulate(rig, 1.0f / 120.0f, {5, 0, 0});
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        require(finite(sixty.pose()[i]), "finite settled matrices");
        require(near(sixty.pose()[i], oneTwenty.pose()[i], 0.015f), "60 Hz and 120 Hz agree");
    }
    require(near(sixty.pose()[3], base[3]), "hair never changes head pose");
    require(sixty.pose()[5][3].x > base[5][3].x + 0.2f, "wind bends hair in wind direction");
    require(sixty.maxLengthError() < 1e-5f, "bone length constraints preserved");

    require(pose.update(base, actor(rig), {5, 0, 0}, 0), "initial frame");
    for (int frame = 1; frame <= 240; ++frame)
    {
        // 200 m/s: no generic distance threshold silently resets normal motion.
        const glm::vec3 offset(200.0f * frame / 60.0f, 0, 0);
        require(pose.update(base, actor(rig, offset), {-20, 3, 2}, 1.0f / 60.0f), "high-speed motion");
        for (const auto& matrix : pose.pose()) require(finite(matrix), "finite high-speed result");
        require(pose.maxLengthError() < 1e-3f, "high-speed lengths");
        require(pose.lastSubsteps() == 2, "movement does not trigger a hidden reset");
    }
    require(pose.update(base, actor(rig, {800, 0, 0}), {0, 0, 0}, 0, true), "explicit teleport reset");
    for (std::size_t i = 0; i < base.size(); ++i) require(near(pose.pose()[i], base[i]), "reset returns authored base");
    pose.settings.enabled = false;
    auto hybridBase = base;
    for (std::size_t i = 3; i <= 6; ++i) hybridBase[i] = glm::translate(glm::mat4(1), {2, 1, 0}) * hybridBase[i];
    require(pose.update(hybridBase, actor(rig), {99, 0, 0}, 1), "disabled solver accepts future animation/IK base");
    for (std::size_t i = 0; i < base.size(); ++i) require(near(pose.pose()[i], hybridBase[i]), "disabled preserves incoming pose");
    pose.settings.enabled = true;
    require(pose.update(base, actor(rig), {0, 0, 0}, 0), "reenable");
    require(pose.update(base, actor(rig), {0, 0, 0}, 100.0f), "bounded long-frame catch-up");
    require(pose.lastSubsteps() <= pose.settings.maxSubsteps, "bounded simulation work");
    auto invalidBase = base;
    invalidBase[3][0][0] = std::numeric_limits<float>::quiet_NaN();
    require(!pose.update(invalidBase, actor(rig), {0, 0, 0}, 0.01f), "invalid bone input rejected");
    require(!pose.update(base, glm::mat4(0), {0, 0, 0}, 0.01f), "singular actor rejected");
    auto cyclic = rig;
    cyclic.bones[0].parent = 3;
    require(!pose.bind(cyclic), "malformed hierarchy rejected");
    std::cout << "Character pose: rest, wind, frame rate, high speed, reset and hybrid-input checks passed.\n";
}
