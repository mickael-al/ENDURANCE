#ifndef ENDURANCE_PROCEDURAL_TERRAIN_FRUSTUM_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_FRUSTUM_HPP

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <array>
#include <cmath>
#include <limits>

namespace ProceduralTerrain
{
    struct Frustum
    {
        // Unnormalised inward-facing planes. Double precision avoids losing
        // the small signed distance when world coordinates are large.
        std::array<glm::dvec4, 6> planes;
    };

    // OpenGL's default clip volume is -w <= x,y,z <= w. Use the exact
    // projection * view matrix of the relevant camera or shadow pass.
    inline Frustum makeFrustum(const glm::mat4& clipFromWorld) noexcept
    {
        Frustum result;
        const glm::dvec4 last(clipFromWorld[0][3], clipFromWorld[1][3],
                              clipFromWorld[2][3], clipFromWorld[3][3]);
        for (int axis = 0; axis < 3; ++axis)
        {
            const glm::dvec4 row(clipFromWorld[0][axis], clipFromWorld[1][axis],
                                 clipFromWorld[2][axis], clipFromWorld[3][axis]);
            result.planes[2 * axis] = last + row;
            result.planes[2 * axis + 1] = last - row;
        }
        return result;
    }

    // The cube encloses the complete chunk, including its transition mesh.
    // Boundary contacts are retained; this is a conservative visibility test,
    // not an occlusion test. extent is the non-negative chunk size in metres.
    inline bool intersectsFrustum(const Frustum& frustum,
                                  const std::array<float, 3>& origin,
                                  float extent) noexcept
    {
        const double half = static_cast<double>(extent) * 0.5;
        const std::array<double, 3> center{{origin[0] + half, origin[1] + half, origin[2] + half}};
        constexpr double roundoff = 8.0 * std::numeric_limits<float>::epsilon();
        for (const auto& p : frustum.planes)
        {
            const double nx = std::abs(p.x), ny = std::abs(p.y), nz = std::abs(p.z);
            const double support = p.x * center[0] + p.y * center[1] + p.z * center[2]
                                 + p.w + half * (nx + ny + nz);
            // CPU matrices and decoded GPU positions are float32. Preserve a
            // small envelope for their rounding, scaled in plane units so no
            // plane normalisation/square root is needed. The one-metre floor
            // also protects near-origin and zero-size boundary contacts.
            const double magnitude = nx * (std::abs(center[0]) + half + 1.0)
                                   + ny * (std::abs(center[1]) + half + 1.0)
                                   + nz * (std::abs(center[2]) + half + 1.0) + std::abs(p.w);
            if (support < -roundoff * magnitude) return false;
        }
        return true;
    }
}

#endif
