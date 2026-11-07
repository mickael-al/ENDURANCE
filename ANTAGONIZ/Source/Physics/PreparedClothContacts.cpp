#include "Physics/PreparedClothContacts.hpp"
#include "Physics/ClothSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define CLOTH_CONTACT_SSE2 1
#endif

namespace
{
    bool finite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    glm::vec3 unitOr(const glm::vec3& value, const glm::vec3& fallback)
    {
        const float squared = glm::dot(value, value);
        return finite(value) && squared > 1.0e-12f ?
            value / std::sqrt(squared) : fallback;
    }

    // The search directions and their order are independent of the collider
    // snapshot. Construct once, with the same normalization as the old helper.
    const std::array<glm::dvec3, 26> FixedDirections = []
    {
        std::array<glm::dvec3, 26> result{};
        unsigned index = 0;
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    if (x || y || z)
                    {
                        const glm::dvec3 direction(x, y, z);
                        result[index++] = direction / std::sqrt(glm::dot(direction, direction));
                    }
        return result;
    }();
}

void PreparedClothContacts::reserveScratch()
{
    m_directions.reserve(27 + m_capsules.size() + m_planes.size() + 1);
    m_intervals.reserve(m_capsules.size());
}

void PreparedClothContacts::prepare(const std::vector<ClothCapsule>& capsules,
    float thickness, const std::vector<ClothPlane>& planes)
{
    prepareCapsules(capsules, thickness);
    preparePlanes(planes, thickness);
}

void PreparedClothContacts::prepareCapsules(
    const std::vector<ClothCapsule>& capsules, float thickness)
{
    m_surfaceThickness = thickness;
    m_raysReady = false;
    const double padding = std::isfinite(thickness) ?
        (std::max)(0.0, double(thickness)) : 0.0;
    m_capsules.clear();
    m_surfaceCapsules.clear();
    m_capsules.reserve(capsules.size());
    m_surfaceCapsules.reserve(capsules.size());
    for (const auto& source : capsules)
    {
        if (!finite(source.a) || !finite(source.b) ||
            !std::isfinite(source.radius) || source.radius <= 0.0f) continue;
        const double radius = double(source.radius) + padding;
        if (radius > 0.0)
        {
            const glm::dvec3 a(source.a), b(source.b);
            const double length = glm::length(b - a);
            Capsule capsule{};
            capsule.a = a;
            capsule.b = b;
            capsule.axis = length > 1.0e-14 ? (b - a) / length : glm::dvec3(0.0);
            capsule.length = length;
            capsule.radius = radius;
            // A strict conservative broad phase for the double closest-point
            // arithmetic. Round outwards and leave 64 machine epsilons of
            // operand-scaled clearance for axis, endpoint and norm roundoff.
            const glm::dvec3 error = (glm::abs(a) + glm::abs(b) +
                glm::dvec3(length + radius)) * (64.0 * std::numeric_limits<double>::epsilon());
            capsule.minimum = glm::min(a, b) - glm::dvec3(radius) - error;
            capsule.maximum = glm::max(a, b) + glm::dvec3(radius) + error;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                capsule.minimum[axis] = std::nextafter(capsule.minimum[axis], -std::numeric_limits<double>::infinity());
                capsule.maximum[axis] = std::nextafter(capsule.maximum[axis], std::numeric_limits<double>::infinity());
            }
            m_capsules.push_back(capsule);
        }
        const glm::vec3 axis = source.b - source.a;
        const float squared = glm::dot(axis, axis), surfaceRadius = source.radius + thickness;
        m_surfaceCapsules.push_back({source.a, axis,
            glm::min(source.a, source.b) - glm::vec3(surfaceRadius),
            glm::max(source.a, source.b) + glm::vec3(surfaceRadius),
            unitOr(glm::cross(axis, glm::vec3(0, 1, 0)), glm::vec3(1, 0, 0)),
            squared > 1.0e-12f ? 1.0f / squared : 0.0f, surfaceRadius});
    }
    m_surfaceBounds.resize((m_surfaceCapsules.size() + 3) / 4);
    for (std::size_t block = 0; block < m_surfaceBounds.size(); ++block)
    {
        auto& bounds = m_surfaceBounds[block];
        bounds.activeMask = 0;
        for (unsigned lane = 0; lane < 4; ++lane)
        {
            const auto index = block * 4 + lane;
            if (index < m_surfaceCapsules.size())
            {
                bounds.activeMask |= 1u << lane;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    bounds.minimum[axis][lane] = m_surfaceCapsules[index].minimum[axis];
                    bounds.maximum[axis][lane] = m_surfaceCapsules[index].maximum[axis];
                }
            }
            else
                for (unsigned axis = 0; axis < 3; ++axis)
                    bounds.minimum[axis][lane] = bounds.maximum[axis][lane] = 0.0f;
        }
    }
    reserveScratch();
}

void PreparedClothContacts::preparePlanes(
    const std::vector<ClothPlane>& planes, float thickness)
{
    m_surfaceThickness = thickness;
    const double padding = std::isfinite(thickness) ?
        (std::max)(0.0, double(thickness)) : 0.0;
    m_planes.clear();
    m_surfacePlanes.clear();
    m_planes.reserve(planes.size());
    m_surfacePlanes.reserve(planes.size());
    for (const auto& source : planes)
    {
        if (!finite(source.normal) || !std::isfinite(source.offset)) continue;
        const double length = glm::length(glm::dvec3(source.normal));
        if (length > 1.0e-14)
        {
            Plane plane{};
            plane.normal = glm::dvec3(source.normal) / length;
            plane.offset = double(source.offset) / length + padding;
            for (std::size_t i = 0; i < FixedDirections.size(); ++i)
                plane.slopes[i] = glm::dot(plane.normal, FixedDirections[i]);
            m_planes.push_back(plane);
        }
        const float squared = glm::dot(source.normal, source.normal);
        const float surfaceLength = std::sqrt(squared);
        m_surfacePlanes.push_back({source.normal, source.offset, surfaceLength,
            squared >= 1.0e-12f, surfaceLength > 1.0e-6f});
    }
    reserveScratch();
}

void PreparedClothContacts::prepareRays()
{
    if (m_raysReady) return;
    m_fixedRays.resize(m_capsules.size());
    for (std::size_t c = 0; c < m_capsules.size(); ++c)
        for (std::size_t i = 0; i < FixedDirections.size(); ++i)
        {
            const auto& capsule = m_capsules[c];
            const auto& direction = FixedDirections[i];
            Ray& ray = m_fixedRays[c][i];
            ray.speedSquared = glm::dot(direction, direction);
            ray.axialSpeed = glm::dot(direction, capsule.axis);
            ray.radialDirection = direction - capsule.axis * ray.axialSpeed;
            ray.radialSpeedSquared = glm::dot(ray.radialDirection, ray.radialDirection);
        }
    m_raysReady = true;
}

glm::dvec3 PreparedClothContacts::closest(
    const glm::dvec3& position, const Capsule& capsule)
{
    return capsule.a + capsule.axis * std::clamp(
        glm::dot(position - capsule.a, capsule.axis), 0.0, capsule.length);
}

// Quadratic interval |origin + t * direction| <= radius. Closest approach
// avoids cancellation near distant surfaces; all original tolerances remain.
bool PreparedClothContacts::radialInterval(const glm::dvec3& origin,
    const glm::dvec3& direction, double speedSquared, double radius, Interval& result)
{
    if (speedSquared <= 1.0e-28)
    {
        if (glm::dot(origin, origin) > radius * radius) return false;
        result = {-std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
        return true;
    }
    const double centre = -glm::dot(origin, direction) / speedSquared;
    const glm::dvec3 closest = origin + direction * centre;
    const double remaining = radius * radius - glm::dot(closest, closest);
    if (remaining < 0.0) return false;
    const double extent = std::sqrt(remaining / speedSquared);
    result = {centre - extent, centre + extent};
    return true;
}

bool PreparedClothContacts::capsuleInterval(const glm::dvec3& position,
    const glm::dvec3& direction, const Capsule& capsule, const Ray* ray, Interval& result)
{
    bool found = false;
    const auto append = [&](const Interval& interval)
    {
        if (interval.lower > interval.upper) return;
        if (!found) { result = interval; found = true; }
        else
        {
            result.lower = (std::min)(result.lower, interval.lower);
            result.upper = (std::max)(result.upper, interval.upper);
        }
    };
    Interval interval{};
    const double speedSquared = ray ? ray->speedSquared : glm::dot(direction, direction);
    if (radialInterval(position - capsule.a, direction, speedSquared,
        capsule.radius, interval)) append(interval);
    if (capsule.length <= 1.0e-14) return found;
    if (radialInterval(position - capsule.b, direction, speedSquared,
        capsule.radius, interval)) append(interval);

    const glm::dvec3 relative = position - capsule.a;
    const double axialPosition = glm::dot(relative, capsule.axis);
    const double axialSpeed = ray ? ray->axialSpeed : glm::dot(direction, capsule.axis);
    const glm::dvec3 radialPosition = relative - capsule.axis * axialPosition;
    const glm::dvec3 radialDirection = ray ? ray->radialDirection : direction - capsule.axis * axialSpeed;
    const double radialSpeedSquared = ray ? ray->radialSpeedSquared : glm::dot(radialDirection, radialDirection);
    if (radialInterval(radialPosition, radialDirection, radialSpeedSquared,
        capsule.radius, interval))
    {
        if (std::abs(axialSpeed) > 1.0e-14)
        {
            double first = -axialPosition / axialSpeed;
            double second = (capsule.length - axialPosition) / axialSpeed;
            if (first > second) std::swap(first, second);
            interval.lower = (std::max)(interval.lower, first);
            interval.upper = (std::min)(interval.upper, second);
            append(interval);
        }
        else if (axialPosition >= 0.0 && axialPosition <= capsule.length)
            append(interval);
    }
    return found;
}

bool PreparedClothContacts::valid(const glm::vec3& position) const
{
    if (!finite(position)) return false;
    const glm::dvec3 sample(position);
    for (const auto& capsule : m_capsules)
    {
        if (sample.x < capsule.minimum.x || sample.y < capsule.minimum.y ||
            sample.z < capsule.minimum.z || sample.x > capsule.maximum.x ||
            sample.y > capsule.maximum.y || sample.z > capsule.maximum.z) continue;
        const glm::dvec3 delta = sample - closest(sample, capsule);
        if (glm::dot(delta, delta) < capsule.radius * capsule.radius) return false;
    }
    for (const auto& plane : m_planes)
        if (glm::dot(plane.normal, sample) < plane.offset) return false;
    return true;
}

glm::vec3 PreparedClothContacts::projectOutside(const glm::vec3& position,
    const glm::vec3& preferredDirection)
{
    if (!finite(position) || valid(position)) return position;
    prepareRays();
    const glm::dvec3 origin(position);
    m_directions.clear();
    const auto addDirection = [&](const glm::dvec3& direction)
    {
        const double length2 = glm::dot(direction, direction);
        if (std::isfinite(length2) && length2 > 1.0e-28)
            m_directions.push_back({direction / std::sqrt(length2), -1});
    };
    // Previous side wins only when corrections are equally short.
    if (finite(preferredDirection)) addDirection(glm::dvec3(preferredDirection));
    for (std::size_t i = 0; i < FixedDirections.size(); ++i)
        m_directions.push_back({FixedDirections[i], int(i)});
    for (const auto& capsule : m_capsules)
    {
        if (origin.x < capsule.minimum.x || origin.y < capsule.minimum.y ||
            origin.z < capsule.minimum.z || origin.x > capsule.maximum.x ||
            origin.y > capsule.maximum.y || origin.z > capsule.maximum.z) continue;
        const glm::dvec3 normal = origin - closest(origin, capsule);
        if (glm::dot(normal, normal) < capsule.radius * capsule.radius)
            addDirection(normal);
    }
    glm::dvec3 violatedPlaneDirection(0.0);
    for (const auto& plane : m_planes)
    {
        addDirection(plane.normal);
        const double violation = plane.offset - glm::dot(plane.normal, origin);
        if (violation > 0.0) violatedPlaneDirection += plane.normal * violation;
    }
    addDirection(violatedPlaneDirection);

    double bestDistance2 = std::numeric_limits<double>::infinity();
    glm::vec3 best = position;
    for (const auto& preparedDirection : m_directions)
    {
        const auto& direction = preparedDirection.vector;
        const int fixed = preparedDirection.fixedIndex;
        double lower = 0.0, upper = std::numeric_limits<double>::infinity();
        bool feasible = true;
        for (const auto& plane : m_planes)
        {
            const double slope = fixed >= 0 ? plane.slopes[std::size_t(fixed)] : glm::dot(plane.normal, direction);
            const double gap = plane.offset - glm::dot(plane.normal, origin);
            if (std::abs(slope) <= 1.0e-14)
            { if (gap > 0.0) { feasible = false; break; } }
            else if (slope > 0.0) lower = (std::max)(lower, gap / slope);
            else upper = (std::min)(upper, gap / slope);
        }
        if (!feasible || lower > upper || lower * lower >= bestDistance2) continue;
        m_intervals.clear();
        for (std::size_t c = 0; c < m_capsules.size(); ++c)
        {
            const auto& capsule = m_capsules[c];
            Interval interval{};
            const Ray* ray = fixed >= 0 ? &m_fixedRays[c][std::size_t(fixed)] : nullptr;
            if (capsuleInterval(origin, direction, capsule, ray, interval) &&
                interval.upper >= lower) m_intervals.push_back(interval);
        }
        std::sort(m_intervals.begin(), m_intervals.end(),
            [](const Interval& a, const Interval& b) { return a.lower < b.lower; });
        double exit = lower, nextEntry = std::numeric_limits<double>::infinity();
        for (const auto& interval : m_intervals)
        {
            if (interval.lower > exit) { nextEntry = interval.lower; break; }
            exit = (std::max)(exit, interval.upper);
        }
        if (!std::isfinite(exit) || exit > upper || exit * exit >= bestDistance2) continue;
        const double gapEnd = (std::min)(nextEntry, upper);
        const double available = gapEnd - exit;
        const glm::dvec3 boundary = origin + direction * exit;
        const double magnitude = (std::max)({std::abs(boundary.x),
            std::abs(boundary.y), std::abs(boundary.z), std::abs(exit), 1.0e-12});
        const double clearance = magnitude * 0.25 * double(std::numeric_limits<float>::epsilon());
        for (unsigned attempt = 0; attempt < 6; ++attempt)
        {
            double extra = attempt == 0 ? 0.0 : clearance * double(1u << (attempt - 1u));
            if (std::isfinite(available))
                extra = (std::min)(extra, (std::max)(0.0, available) * 0.5);
            const double distance = exit + extra;
            if (distance > upper) break;
            const glm::vec3 candidate(origin + direction * distance);
            if (!valid(candidate)) continue;
            const glm::dvec3 displacement = glm::dvec3(candidate) - origin;
            const double distance2 = glm::dot(displacement, displacement);
            if (distance2 < bestDistance2)
            { bestDistance2 = distance2; best = candidate; }
            break;
        }
    }
    return best;
}

unsigned PreparedClothContacts::surfaceMask(std::size_t block, const glm::vec3& position) const
{
    const auto& bounds = m_surfaceBounds[block];
#if defined(CLOTH_CONTACT_SSE2)
    const __m128 x = _mm_set1_ps(position.x);
    const __m128 y = _mm_set1_ps(position.y);
    const __m128 z = _mm_set1_ps(position.z);
    __m128 outside = _mm_or_ps(_mm_cmplt_ps(x, _mm_loadu_ps(bounds.minimum[0].data())),
        _mm_cmpgt_ps(x, _mm_loadu_ps(bounds.maximum[0].data())));
    outside = _mm_or_ps(outside, _mm_or_ps(_mm_cmplt_ps(y, _mm_loadu_ps(bounds.minimum[1].data())),
        _mm_cmpgt_ps(y, _mm_loadu_ps(bounds.maximum[1].data()))));
    outside = _mm_or_ps(outside, _mm_or_ps(_mm_cmplt_ps(z, _mm_loadu_ps(bounds.minimum[2].data())),
        _mm_cmpgt_ps(z, _mm_loadu_ps(bounds.maximum[2].data()))));
    return ~unsigned(_mm_movemask_ps(outside)) & bounds.activeMask;
#else
    unsigned mask = 0;
    for (unsigned lane = 0; lane < 4; ++lane)
        if ((bounds.activeMask & (1u << lane)) &&
            !(position.x < bounds.minimum[0][lane] || position.y < bounds.minimum[1][lane] ||
              position.z < bounds.minimum[2][lane] || position.x > bounds.maximum[0][lane] ||
              position.y > bounds.maximum[1][lane] || position.z > bounds.maximum[2][lane]))
            mask |= 1u << lane;
    return mask;
#endif
}

bool PreparedClothContacts::projectSurfaceCapsule(std::size_t index,
    glm::vec3& position, const glm::vec3& fallbackNormal) const
{
    const auto& capsule = m_surfaceCapsules[index];
    const float t = std::clamp(glm::dot(position - capsule.a, capsule.axis) *
        capsule.inverseLengthSquared, 0.0f, 1.0f);
    const glm::vec3 axisPoint = capsule.a + capsule.axis * t;
    const glm::vec3 difference = position - axisPoint;
    const float squared = glm::dot(difference, difference);
    if (squared >= capsule.radius * capsule.radius) return false;
    glm::vec3 fallback = fallbackNormal - capsule.axis *
        (glm::dot(fallbackNormal, capsule.axis) * capsule.inverseLengthSquared);
    fallback = unitOr(fallback, capsule.axialFallback);
    const glm::vec3 normal = unitOr(difference, fallback);
    position = axisPoint + normal * capsule.radius;
    return true;
}

void PreparedClothContacts::projectSurface(glm::vec3& position, const glm::vec3& fallbackNormal)
{
    const glm::vec3 original = position;
    for (unsigned iteration = 0; iteration < 8; ++iteration)
    {
        bool moved = false;
        for (std::size_t block = 0; block < m_surfaceBounds.size(); ++block)
        {
            unsigned remaining = m_surfaceBounds[block].activeMask;
            unsigned mask = surfaceMask(block, position);
            while (mask)
            {
                unsigned lane = 0;
                while ((mask & (1u << lane)) == 0) ++lane;
                remaining &= ~((1u << (lane + 1u)) - 1u);
                if (projectSurfaceCapsule(block * 4 + lane, position, fallbackNormal))
                {
                    moved = true;
                    // A correction can enter a later capsule of this block.
                    // Re-evaluate it before continuing, preserving scalar order.
                    mask = surfaceMask(block, position) & remaining;
                }
                else mask &= ~(1u << lane);
            }
        }
        for (const SurfacePlane& plane : m_surfacePlanes)
        {
            if (!plane.projectable) continue;
            const float distance = glm::dot(plane.normal, position) / plane.length - plane.offset / plane.length;
            if (distance < m_surfaceThickness)
            {
                position += plane.normal * ((m_surfaceThickness - distance) / plane.length);
                moved = true;
            }
        }
        if (!moved) break;
    }
    bool residual = false;
    for (std::size_t block = 0; block < m_surfaceBounds.size() && !residual; ++block)
    {
        unsigned mask = surfaceMask(block, position);
        while (mask)
        {
            unsigned lane = 0;
            while ((mask & (1u << lane)) == 0) ++lane;
            mask &= ~(1u << lane);
            const auto& capsule = m_surfaceCapsules[block * 4 + lane];
            const float t = std::clamp(glm::dot(position - capsule.a, capsule.axis) *
                capsule.inverseLengthSquared, 0.0f, 1.0f);
            if (glm::length(position - capsule.a - capsule.axis * t) < capsule.radius - 1.0e-5f)
            { residual = true; break; }
        }
    }
    if (!residual)
        for (const SurfacePlane& plane : m_surfacePlanes)
            if (plane.checkable &&
                (glm::dot(plane.normal, position) - plane.offset) / plane.length < m_surfaceThickness - 1.0e-5f)
            { residual = true; break; }
    if (residual) position = projectOutside(original, fallbackNormal);
}

// Compatibility path for callers that project a single point. Bulk callers
// retain a PreparedClothContacts instance and prepare once per collider sample.
glm::vec3 projectOutsideClothCapsules(const glm::vec3& position,
    const glm::vec3& preferredDirection, const std::vector<ClothCapsule>& capsules,
    float thickness, const std::vector<ClothPlane>& planes)
{
    PreparedClothContacts prepared;
    prepared.prepare(capsules, thickness, planes);
    return prepared.projectOutside(position, preferredDirection);
}
