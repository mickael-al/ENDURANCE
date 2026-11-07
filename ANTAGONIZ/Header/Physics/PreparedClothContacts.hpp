#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>

struct ClothCapsule;
struct ClothPlane;

// One snapshot of collision geometry, owned by a single simulation worker.
// Prepare after geometry/thickness changes, then project any number of points.
// All per-point scratch buffers retain capacity; no shared mutable state.
class PreparedClothContacts
{
public:
    void prepare(const std::vector<ClothCapsule>& capsules, float thickness,
        const std::vector<ClothPlane>& planes = {});
    // Partial preparation shares one thickness: refresh both sets whenever it
    // changes; otherwise collider geometry can be refreshed independently.
    void prepareCapsules(const std::vector<ClothCapsule>& capsules, float thickness);
    void preparePlanes(const std::vector<ClothPlane>& planes, float thickness);

    // Exact double-precision escape from a union of overlapping capsules while
    // preserving the plane halfspaces. Retains the original helper's ordering.
    glm::vec3 projectOutside(const glm::vec3& position,
        const glm::vec3& preferredDirection);
    // Fine surface contact polish, including exact union escape when the
    // original eight sequential projection passes do not converge.
    void projectSurface(glm::vec3& position, const glm::vec3& fallbackNormal);

private:
    struct Interval { double lower, upper; };
    struct Ray
    {
        glm::dvec3 radialDirection;
        double speedSquared, radialSpeedSquared, axialSpeed;
    };
    struct Capsule
    {
        glm::dvec3 a, b, axis, minimum, maximum;
        double length, radius;
    };
    struct Plane
    {
        glm::dvec3 normal;
        double offset;
        std::array<double, 26> slopes;
    };
    struct SurfaceCapsule
    {
        glm::vec3 a, axis, minimum, maximum, axialFallback;
        float inverseLengthSquared, radius;
    };
    struct SurfaceBounds
    {
        std::array<float, 4> minimum[3], maximum[3];
        unsigned activeMask = 0;
    };
    struct SurfacePlane
    {
        glm::vec3 normal;
        float offset, length;
        bool projectable, checkable;
    };
    struct Direction { glm::dvec3 vector; int fixedIndex; };

    static glm::dvec3 closest(const glm::dvec3& position, const Capsule& capsule);
    static bool radialInterval(const glm::dvec3& origin,
        const glm::dvec3& direction, double speedSquared, double radius,
        Interval& result);
    static bool capsuleInterval(const glm::dvec3& position,
        const glm::dvec3& direction, const Capsule& capsule, const Ray* ray,
        Interval& result);
    bool valid(const glm::vec3& position) const;
    void reserveScratch();
    void prepareRays();
    unsigned surfaceMask(std::size_t block, const glm::vec3& position) const;
    bool projectSurfaceCapsule(std::size_t index, glm::vec3& position,
        const glm::vec3& fallbackNormal) const;

    std::vector<Capsule> m_capsules;
    std::vector<std::array<Ray, 26>> m_fixedRays;
    bool m_raysReady = false;
    std::vector<Plane> m_planes;
    std::vector<SurfaceCapsule> m_surfaceCapsules;
    std::vector<SurfaceBounds> m_surfaceBounds;
    std::vector<SurfacePlane> m_surfacePlanes;
    std::vector<Direction> m_directions;
    std::vector<Interval> m_intervals;
    float m_surfaceThickness = 0.0f;
};
