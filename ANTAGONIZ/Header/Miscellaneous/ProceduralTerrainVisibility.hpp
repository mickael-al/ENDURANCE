#ifndef ENDURANCE_PROCEDURAL_TERRAIN_VISIBILITY_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_VISIBILITY_HPP

#include "ProceduralTerrainFrustum.hpp"
#include <glm/vec3.hpp>
#include <cstdint>
#include <cstring>

namespace ProceduralTerrain
{
    namespace VisibilityDetail
    {
        inline bool sameFloat(float a, float b) noexcept
        {
            // Bit comparisons stay exact with the engine's -ffast-math flags.
            std::uint32_t x, y;
            std::memcpy(&x, &a, sizeof(x));
            std::memcpy(&y, &b, sizeof(y));
            return x == y;
        }

        inline bool sameMatrix(const glm::mat4& a, const glm::mat4& b) noexcept
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!sameFloat(a[column][row], b[column][row])) return false;
            return true;
        }

        inline bool sameViewBasis(const glm::mat4& a, const glm::mat4& b) noexcept
        {
            // Ignore only the affine translation column. Rotation, scale and
            // the homogeneous row must remain unchanged to reuse the frustum.
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!(column == 3 && row < 3) &&
                        !sameFloat(a[column][row], b[column][row])) return false;
            return true;
        }

        inline Frustum expandedFrustum(const glm::mat4& projection, const glm::mat4& view,
                                       double metres) noexcept
        {
            // The vertex shader transforms view * position, then projection.
            // Compose these float matrices in double on the CPU: composing in
            // float first can corrupt the far plane by cancellation far from
            // the origin, before makeFrustum's conservative test even runs.
            const glm::dmat4 clipFromWorld = glm::dmat4(projection) * glm::dmat4(view);
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
            for (auto& plane : result.planes)
                plane.w += metres * std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
            return result;
        }
    }

    // Owns the cache key and its conservative frustum, not the mesh list. When
    // update() returns true, rebuild that list from the current visible coverage
    // and frustum(). Otherwise reuse the list unchanged, without retesting AABBs.
    class CameraVisibilityCache
    {
    public:
        // position is the world position of the camera represented by affine
        // view. translationStep is a non-negative finite world-space distance.
        // Rotation/projection/coverage changes always refresh immediately.
        bool update(const void* cameraIdentity, const glm::mat4& view,
                    const glm::mat4& projection, const glm::vec3& position,
                    std::uint64_t visibleRevision, float translationStep) noexcept
        {
            const double step = translationStep > 0.0f ? translationStep : 0.0;
            const double dx = static_cast<double>(position.x) - m_position.x;
            const double dy = static_cast<double>(position.y) - m_position.y;
            const double dz = static_cast<double>(position.z) - m_position.z;
            const double distanceSquared = dx * dx + dy * dy + dz * dz;
            const bool moved = step > 0.0 ? distanceSquared >= step * step : distanceSquared > 0.0;
            if (m_valid && cameraIdentity == m_cameraIdentity && visibleRevision == m_revision &&
                VisibilityDetail::sameFloat(translationStep, m_step) && !moved &&
                VisibilityDetail::sameViewBasis(view, m_view) &&
                VisibilityDetail::sameMatrix(projection, m_projection)) return false;

            // For a fixed camera basis/projection, translating by d changes a
            // plane offset by at most |normal|*|d|. Expanding all six planes by
            // the full step keeps every chunk that could enter before refresh.
            // The existing frustum test also includes its float roundoff margin.
            m_frustum = VisibilityDetail::expandedFrustum(projection, view, step);
            m_cameraIdentity = cameraIdentity;
            m_view = view;
            m_projection = projection;
            m_position = position;
            m_step = translationStep;
            m_revision = visibleRevision;
            m_valid = true;
            return true;
        }

        const Frustum& frustum() const noexcept { return m_frustum; }
        void invalidate() noexcept { m_valid = false; }

    private:
        const void* m_cameraIdentity = nullptr;
        glm::mat4 m_view{1.0f}, m_projection{1.0f};
        glm::vec3 m_position{0.0f};
        Frustum m_frustum{};
        std::uint64_t m_revision = 0;
        float m_step = 0.0f;
        bool m_valid = false;
    };

    // Keep one instance and one mesh list per shadow index. A shadow matrix
    // change (moving sun/camera/cascade) always refreshes; no stale light frustum
    // is reused merely because the main camera has not crossed its movement step.
    class ShadowVisibilityCache
    {
    public:
        bool update(const glm::mat4& lightMatrix, std::uint64_t visibleRevision) noexcept
        {
            if (m_valid && visibleRevision == m_revision &&
                VisibilityDetail::sameMatrix(lightMatrix, m_matrix)) return false;
            m_frustum = makeFrustum(lightMatrix);
            m_matrix = lightMatrix;
            m_revision = visibleRevision;
            m_valid = true;
            return true;
        }

        const Frustum& frustum() const noexcept { return m_frustum; }
        void invalidate() noexcept { m_valid = false; }

    private:
        glm::mat4 m_matrix{1.0f};
        Frustum m_frustum{};
        std::uint64_t m_revision = 0;
        bool m_valid = false;
    };
}

#endif
