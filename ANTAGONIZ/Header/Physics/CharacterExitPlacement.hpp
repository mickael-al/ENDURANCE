#ifndef PARASITE_CHARACTER_EXIT_PLACEMENT_HPP
#define PARASITE_CHARACTER_EXIT_PLACEMENT_HPP

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Ge
{
struct CharacterExitRequest
{
    glm::vec3 hostPosition{0.0f};
    float yaw = 0.0f;
    float hostRadius = 0.35f;
    float hostHalfHeight = 0.9f;
    float radius = 0.24f;
    float halfHeight = 0.5f;
    std::vector<glm::vec3> recoveryPositions;
};

// Query supplies groundRay(from, to, point, normal), clearCapsule(centre),
// and clearPath(from, to). Keeping the search independent of Bullet also lets
// the same decision logic be tested against small, deterministic scenes.
template<class Query>
bool findCharacterExitPlacement(const CharacterExitRequest& request, Query& query,
    glm::vec3& result)
{
    const auto finite = [](glm::vec3 p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    if (!finite(request.hostPosition) || !std::isfinite(request.yaw) ||
        !std::isfinite(request.radius) || !std::isfinite(request.halfHeight) ||
        !std::isfinite(request.hostRadius) || !std::isfinite(request.hostHalfHeight) ||
        request.radius <= 0.0f || request.halfHeight < request.radius ||
        request.hostRadius <= 0.0f || request.hostHalfHeight < request.hostRadius)
        return false;

    constexpr float GroundSkin = 0.06f;
    constexpr float MaximumStepUp = 0.35f;
    constexpr float MaximumDrop = 1.25f;
    constexpr float MinimumGroundNormal = 0.70710678f;
    constexpr float Pi = 3.14159265359f;
    const auto floorAt = [&](glm::vec3 horizontal, float feetY, glm::vec3& floor)
    {
        glm::vec3 normal(0.0f);
        const glm::vec3 from(horizontal.x, feetY + MaximumStepUp + GroundSkin, horizontal.z);
        const glm::vec3 to(horizontal.x, feetY - MaximumDrop, horizontal.z);
        return query.groundRay(from, to, floor, normal) && finite(floor) && finite(normal) &&
            normal.y >= MinimumGroundNormal && floor.y <= feetY + MaximumStepUp &&
            floor.y >= feetY - MaximumDrop;
    };
    const auto grounded = [&](glm::vec3 horizontal, float feetY, glm::vec3& centre)
    {
        glm::vec3 floor;
        if (!floorAt(horizontal, feetY, floor)) return false;
        // A centre ray alone can accept a ledge with most of the capsule over
        // empty space. Sample the footprint too; tolerate ordinary stair risers.
        float highest = floor.y;
        for (int sample = 0; sample < 8; ++sample)
        {
            const float angle = sample * Pi / 4.0f;
            const glm::vec3 offset(std::cos(angle) * request.radius, 0.0f,
                std::sin(angle) * request.radius);
            glm::vec3 support;
            if (!floorAt(horizontal + offset, feetY, support) ||
                std::abs(support.y - floor.y) > 0.25f) return false;
            highest = (std::max)(highest, support.y);
        }
        centre = glm::vec3(horizontal.x, highest + request.halfHeight + GroundSkin, horizontal.z);
        return query.clearCapsule(centre);
    };

    const float feetY = request.hostPosition.y - request.hostHalfHeight;
    const float minimumSeparation = request.hostRadius + request.radius + 0.12f;
    const std::array<float, 3> rings{{(std::max)(0.9f, minimumSeparation),
        (std::max)(1.35f, minimumSeparation), (std::max)(1.8f, minimumSeparation)}};
    // Preserve the old right-side preference when safe, then look around the
    // host rather than teleporting through a wall to a valid-looking floor.
    for (float distance : rings)
    {
        for (int sample = 0; sample < 16; ++sample)
        {
            const int step = sample == 0 ? 0 : ((sample + 1) / 2) * (sample % 2 ? 1 : -1);
            const float angle = request.yaw * Pi / 180.0f + step * Pi / 8.0f;
            const glm::vec3 horizontal = request.hostPosition +
                glm::vec3(std::cos(angle), 0.0f, -std::sin(angle)) * distance;
            glm::vec3 candidate;
            if (grounded(horizontal, feetY, candidate) &&
                query.clearPath(request.hostPosition, candidate))
            {
                result = candidate;
                return true;
            }
        }
    }

    // Above the head is useful in a cramped corner, but never through a low
    // ceiling or above a host that is already falling into empty space.
    glm::vec3 hostFloor;
    const glm::vec3 above = request.hostPosition +
        glm::vec3(0.0f, request.hostHalfHeight + request.halfHeight + GroundSkin, 0.0f);
    if (floorAt(request.hostPosition, feetY, hostFloor) && query.clearCapsule(above) &&
        query.clearPath(request.hostPosition, above))
    {
        result = above;
        return true;
    }

    // Last resort: authored spawns, validated against the current world (a prop
    // may now occupy one). No reachability check: this is an emergency respawn.
    for (glm::vec3 spawn : request.recoveryPositions)
    {
        glm::vec3 candidate;
        if (finite(spawn) && grounded(spawn, spawn.y - request.hostHalfHeight, candidate))
        {
            result = candidate;
            return true;
        }
    }
    return false; // No unvalidated teleport when the world is unavailable.
}
}
#endif
