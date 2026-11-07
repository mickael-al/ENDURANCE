#include "Physics/ClothSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define CLOTH_SSE2_BROADPHASE 1
#endif

namespace
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float HardPin = ClothHardPinWeight;

    bool finite(const glm::vec3& v)
    { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

    float positive(float v, float fallback)
    { return std::isfinite(v) && v > 0.0f ? v : fallback; }

    float nonnegative(float v, float fallback)
    { return std::isfinite(v) && v >= 0.0f ? v : fallback; }

    float wrapAngle(float value)
    {
        while (value > Pi) value -= 2.0f * Pi;
        while (value < -Pi) value += 2.0f * Pi;
        return value;
    }

    struct Cell
    {
        std::int64_t x, y, z;
        bool operator==(const Cell& other) const
        { return x == other.x && y == other.y && z == other.z; }
    };
    struct CellHash
    {
        std::size_t operator()(const Cell& p) const
        {
            std::size_t h = std::hash<std::int64_t>{}(p.x);
            h ^= std::hash<std::int64_t>{}(p.y) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<std::int64_t>{}(p.z) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            return h;
        }
    };
    Cell cell(const glm::vec3& p, float width)
    {
        // Bounds avoid undefined float-to-integer conversion for bad assets.
        constexpr double bound = double(std::numeric_limits<std::int64_t>::max() / 4);
        const auto axis = [width, bound](float v)
        {
            return std::int64_t(std::clamp(std::floor(double(v) / double(width)),
                -bound, bound));
        };
        return {axis(p.x), axis(p.y), axis(p.z)};
    }

    std::uint64_t edgeKey(std::uint32_t a, std::uint32_t b)
    {
        if (a > b) std::swap(a, b);
        return (std::uint64_t(a) << 32u) | b;
    }
    struct TriangleKey
    {
        std::array<std::uint32_t, 3> vertices;
        bool operator==(const TriangleKey& other) const
        { return vertices == other.vertices; }
    };
    struct TriangleHash
    {
        std::size_t operator()(const TriangleKey& t) const
        {
            return std::size_t(t.vertices[0]) * 73856093u ^
                std::size_t(t.vertices[1]) * 19349663u ^
                std::size_t(t.vertices[2]) * 83492791u;
        }
    };

    unsigned capsuleBroadphaseMask(const glm::vec3& pointMinimum,
        const glm::vec3& pointMaximum, const std::array<glm::vec4, 6>& bounds)
    {
#if defined(CLOTH_SSE2_BROADPHASE)
        __m128 rejected = _mm_cmplt_ps(_mm_set1_ps(pointMaximum.x), _mm_loadu_ps(&bounds[0].x));
        rejected = _mm_or_ps(rejected, _mm_cmplt_ps(_mm_set1_ps(pointMaximum.y), _mm_loadu_ps(&bounds[1].x)));
        rejected = _mm_or_ps(rejected, _mm_cmplt_ps(_mm_set1_ps(pointMaximum.z), _mm_loadu_ps(&bounds[2].x)));
        rejected = _mm_or_ps(rejected, _mm_cmpgt_ps(_mm_set1_ps(pointMinimum.x), _mm_loadu_ps(&bounds[3].x)));
        rejected = _mm_or_ps(rejected, _mm_cmpgt_ps(_mm_set1_ps(pointMinimum.y), _mm_loadu_ps(&bounds[4].x)));
        rejected = _mm_or_ps(rejected, _mm_cmpgt_ps(_mm_set1_ps(pointMinimum.z), _mm_loadu_ps(&bounds[5].x)));
        return (~unsigned(_mm_movemask_ps(rejected))) & 15u;
#else
        unsigned mask = 0;
        for (unsigned lane = 0; lane < 4; ++lane)
            if (!(pointMaximum.x < bounds[0][lane] || pointMinimum.x > bounds[3][lane] ||
                pointMaximum.y < bounds[1][lane] || pointMinimum.y > bounds[4][lane] ||
                pointMaximum.z < bounds[2][lane] || pointMinimum.z > bounds[5][lane]))
                mask |= 1u << lane;
        return mask;
#endif
    }

    // The two triangles have consistent winding (a,b,c) and (b,a,d).
    bool dihedral(const glm::vec3& a, const glm::vec3& b,
        const glm::vec3& c, const glm::vec3& d, float& angle,
        std::array<glm::vec3, 4>* gradient = nullptr)
    {
        const glm::vec3 edge = b - a;
        const glm::vec3 first = glm::cross(edge, c - a);
        const glm::vec3 second = glm::cross(d - a, edge);
        const float edge2 = glm::dot(edge, edge);
        const float first2 = glm::dot(first, first), second2 = glm::dot(second, second);
        if (edge2 <= 1.0e-16f || first2 <= 1.0e-20f || second2 <= 1.0e-20f)
            return false;
        const float length = std::sqrt(edge2);
        const glm::vec3 n1 = first / std::sqrt(first2), n2 = second / std::sqrt(second2);
        angle = std::atan2(glm::dot(glm::cross(n1, n2), edge / length),
            std::clamp(glm::dot(n1, n2), -1.0f, 1.0f));
        if (gradient)
        {
            (*gradient)[2] = -length * first / first2;
            (*gradient)[3] = -length * second / second2;
            (*gradient)[0] = glm::dot(c - b, edge) / edge2 * (*gradient)[2] +
                glm::dot(d - b, edge) / edge2 * (*gradient)[3];
            (*gradient)[1] = -((*gradient)[0] + (*gradient)[2] + (*gradient)[3]);
        }
        return true;
    }

    ClothCapsule cleanCapsule(const ClothCapsule& source)
    {
        ClothCapsule c = source;
        if (!finite(c.a)) c.a = glm::vec3(0.0f);
        if (!finite(c.b)) c.b = c.a;
        c.radius = nonnegative(c.radius, 0.0f);
        c.friction = std::clamp(nonnegative(c.friction, 0.0f), 0.0f, 1.0f);
        return c;
    }
    ClothCapsule mixCapsule(const ClothCapsule& a, const ClothCapsule& b, float t)
    {
        ClothCapsule result;
        result.a = glm::mix(a.a, b.a, t);
        result.b = glm::mix(a.b, b.b, t);
        result.radius = a.radius + (b.radius - a.radius) * t;
        result.friction = b.friction;
        result.response = b.response;
        return result;
    }
    glm::vec3 capsulePoint(const glm::vec3& p, const ClothCapsule& c, float& t)
    {
        const glm::vec3 axis = c.b - c.a;
        const float length2 = glm::dot(axis, axis);
        t = length2 > 1.0e-16f ? std::clamp(glm::dot(p - c.a, axis) / length2,
            0.0f, 1.0f) : 0.0f;
        return c.a + axis * t;
    }
    // Static endpoint geometry is identical for every particle in a substep.
    // Keep the original dot/division sequence; reciprocal multiplication would
    // change rounding, and mixed CCD samples still use capsulePoint above.
    glm::vec3 capsulePointCached(const glm::vec3& p, const ClothCapsule& c,
        const glm::vec4& axisAndLength, float& t)
    {
        const glm::vec3 axis(axisAndLength);
        const float length2 = axisAndLength.w;
        t = length2 > 1.0e-16f ? std::clamp(glm::dot(p - c.a, axis) / length2,
            0.0f, 1.0f) : 0.0f;
        return c.a + axis * t;
    }

    glm::vec3 safeNormal(const glm::vec3& difference, const glm::vec3& oldDifference,
        const glm::vec3& axis)
    {
        float length2 = glm::dot(difference, difference);
        if (length2 > 1.0e-16f) return difference / std::sqrt(length2);
        length2 = glm::dot(oldDifference, oldDifference);
        if (length2 > 1.0e-16f) return oldDifference / std::sqrt(length2);
        glm::vec3 normal = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(normal, normal) < 1.0e-10f) normal = glm::vec3(1.0f, 0.0f, 0.0f);
        return glm::normalize(normal);
    }

    // Closest point with barycentric coordinates, including boundary cases.
    glm::vec3 closestTriangle(const glm::vec3& p, const glm::vec3& a,
        const glm::vec3& b, const glm::vec3& c)
    {
        const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
        const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return {1, 0, 0};
        const glm::vec3 bp = p - b;
        const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return {0, 1, 0};
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        { const float v = d1 / (d1 - d3); return {1.0f - v, v, 0}; }
        const glm::vec3 cp = p - c;
        const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return {0, 0, 1};
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        { const float w = d2 / (d2 - d6); return {1.0f - w, 0, w}; }
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
        { const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return {0, 1.0f - w, w}; }
        const float sum = va + vb + vc;
        if (std::abs(sum) <= 1.0e-20f) return {1, 0, 0};
        const float v = vb / sum, w = vc / sum;
        return {1.0f - v - w, v, w};
    }

    // Equal-endpoint capsules are spheres. Solve their relative-motion
    // quadratic once instead of the general capsule's iterative distance
    // search. The radius may change too (combat helpers can enlarge smoothly).
    // Squared-distance rejection avoids normalizing exterior points; only a
    // real swept/current contact takes square roots.
    bool collideSphere(const glm::vec3& oldPosition, glm::vec3& position,
        const ClothCapsule& before, const ClothCapsule& after, float thickness,
        const glm::vec3& travel, const std::array<glm::vec4, 5>& cache,
        std::array<glm::vec4, 4>& startCache, unsigned char& startValid,
        glm::vec3& normal, glm::vec3& surfaceVelocity)
    {
        if (!(startValid & 1))
        {
            const auto relative = oldPosition - before.a;
            startCache[1] = glm::vec4(relative, 0.0f);
            startValid |= 1;
        }
        const glm::vec3 relative(startCache[1]);
        const glm::vec3 motion(cache[3]);
        const glm::vec3 relativeTravel = travel - motion;
        const float tolerance = cache[2].w;
        const double radius = double(before.radius) + double(thickness) + tolerance;
        const double radiusDelta = cache[4].w;
        const glm::dvec3 origin(relative), direction(relativeTravel);
        const double c = glm::dot(origin, origin) - radius * radius;
        double hit = c <= 0.0 ? 0.0 : -1.0;
        if (hit < 0.0)
        {
            // |origin + t * direction|^2 <= (radius + t * delta)^2.
            // Use doubles and the stable entering root to preserve narrow
            // contacts during a fast swing, without a fixed iteration cap.
            const double a = glm::dot(direction, direction) - radiusDelta * radiusDelta;
            const double b = glm::dot(origin, direction) - radius * radiusDelta;
            const double end = (a + 2.0 * b) + c;
            // If both endpoints are exterior, only a convex quadratic whose
            // minimum lies inside this time interval can reach the sphere.
            // Reject the others before the discriminant's square root.
            const bool possible = end <= 0.0 || (a > 0.0 && b < 0.0 && -b < a);
            if (possible && a == 0.0)
            {
                if (b < 0.0) hit = -c / (2.0 * b);
            }
            else if (possible)
            {
                const double discriminant = b * b - a * c;
                if (discriminant >= 0.0)
                {
                    const double root = std::sqrt(discriminant);
                    hit = b < 0.0 ? c / (-b + root) : (-b - root) / a;
                }
            }
            if (!(hit >= 0.0 && hit <= 1.0)) hit = -1.0;
        }
        bool contact = false;
        if (hit >= 0.0)
        {
            const float time = float(hit);
            const glm::vec3 difference = relative + relativeTravel * time;
            normal = safeNormal(difference, relative, glm::vec3(0.0f));
            // Radius is a surface motion too. A shrinking helper must leave
            // an exterior resting point behind rather than pulling it inward.
            glm::vec3 remaining = (relativeTravel - normal * float(radiusDelta)) * (1.0f - time);
            remaining -= normal * (std::min)(0.0f, glm::dot(remaining, normal));
            position = after.a + normal * cache[2].z + remaining;
            contact = true;
        }
        const glm::vec3 difference = position - after.a;
        const float distanceSquared = glm::dot(difference, difference);
        const float endRadius = cache[2].z;
        const float paddedRadius = endRadius + tolerance;
        if (distanceSquared < paddedRadius * paddedRadius)
        {
            normal = safeNormal(difference, relative, glm::vec3(0.0f));
            position = after.a + normal * endRadius;
            contact = true;
        }
        if (contact) surfaceVelocity = glm::vec3(cache[4]);
        return contact;
    }

    // Conservative advancement against a linearly moving capsule. Its speed
    // bound includes both segment endpoints, so a fast forearm cannot tunnel
    // through the cloth even when the point ends outside the capsule.
    bool collideCapsule(const glm::vec3& oldPosition, glm::vec3& position,
        const ClothCapsule& before, const ClothCapsule& after, float thickness,
        const glm::vec3& travel, float travelLength,
        const std::array<glm::vec4, 5>& cache, float dt,
        std::array<glm::vec4, 4>& startCache, unsigned char& startValid,
        glm::vec3& normal, glm::vec3& surfaceVelocity)
    {
        if (cache[3].w != 0.0f)
            return collideSphere(oldPosition, position, before, after, thickness,
                travel, cache, startCache, startValid, normal, surfaceVelocity);
        const float speedBound = travelLength + cache[2].x + cache[2].y;
        float hit = -1.0f, segment = 0.0f, time = 0.0f;
        const float tolerance = cache[2].w;
        if (speedBound > tolerance)
        {
            for (unsigned iteration = 0; iteration < 32; ++iteration)
            {
                glm::vec3 closest, difference, axis;
                float distance, gap;
                if (iteration == 0 && (startValid & 1))
                {
                    closest = glm::vec3(startCache[0]);
                    segment = startCache[0].w;
                    difference = glm::vec3(startCache[1]);
                    distance = startCache[1].w;
                    axis = glm::vec3(startCache[2]);
                    gap = startCache[2].w;
                }
                else
                {
                    const ClothCapsule capsule = mixCapsule(before, after, time);
                    const glm::vec3 sample = oldPosition + travel * time;
                    closest = capsulePoint(sample, capsule, segment);
                    difference = sample - closest;
                    distance = glm::length(difference);
                    axis = capsule.b - capsule.a;
                    gap = distance - capsule.radius - thickness;
                    if (iteration == 0)
                    {
                        startCache[0] = glm::vec4(closest, segment);
                        startCache[1] = glm::vec4(difference, distance);
                        startCache[2] = glm::vec4(axis, gap);
                        startValid |= 1;
                    }
                }
                if (gap <= tolerance)
                {
                    normal = glm::dot(difference, difference) > 1.0e-16f ? difference / distance :
                        safeNormal(difference, oldPosition - closest, axis);
                    hit = time;
                    break;
                }
                time += gap / speedBound * 0.90f;
                if (time > 1.0f) break;
            }
        }
        bool contact = false;
        if (hit >= 0.0f)
        {
            const ClothCapsule capsule = mixCapsule(before, after, hit);
            const glm::vec3 atHit = capsule.a + (capsule.b - capsule.a) * segment;
            const glm::vec3 atEnd = after.a + glm::vec3(cache[1]) * segment;
            const glm::vec3 surfaceMove = atEnd - atHit;
            glm::vec3 remaining = travel * (1.0f - hit) - surfaceMove;
            remaining -= normal * (std::min)(0.0f, glm::dot(remaining, normal));
            position = atEnd + normal * cache[2].z + remaining;
            contact = true;
        }
        float currentSegment = 0.0f;
        const glm::vec3 closest = capsulePointCached(position, after, cache[1], currentSegment);
        const glm::vec3 difference = position - closest;
        const float distance = glm::length(difference), radius = cache[2].z;
        if (distance < radius + tolerance)
        {
            if (glm::dot(difference, difference) > 1.0e-16f) normal = difference / distance;
            else
            {
                // The old closest point is solely a degenerate-normal fallback;
                // do not compute it on ordinary contacts. A degenerate pair can
                // still reuse the same old point across structural iterations.
                if (!(startValid & 2))
                {
                    float oldSegment = 0.0f;
                    const glm::vec3 oldClosest = capsulePointCached(oldPosition, before, cache[0], oldSegment);
                    startCache[3] = glm::vec4(oldClosest, oldSegment);
                    startValid |= 2;
                }
                normal = safeNormal(difference, oldPosition - glm::vec3(startCache[3]), glm::vec3(cache[1]));
            }
            position = closest + normal * radius;
            segment = currentSegment;
            contact = true;
        }
        if (contact)
        {
            const glm::vec3 oldSurface = before.a + glm::vec3(cache[0]) * segment;
            const glm::vec3 newSurface = after.a + glm::vec3(cache[1]) * segment;
            surfaceVelocity = (newSurface - oldSurface) / dt;
        }
        return contact;
    }
}

std::shared_ptr<const ClothTopology> ClothTopology::build(
    const std::vector<glm::vec3>& renderPositions,
    const std::vector<std::uint32_t>& renderTriangles,
    const std::vector<float>& renderPins, const ClothBuildSettings& settings)
{
    if (renderPositions.empty() || renderPositions.size() > std::numeric_limits<std::uint32_t>::max() ||
        renderPins.size() != renderPositions.size() || renderTriangles.size() % 3 != 0)
        throw std::invalid_argument("Cloth topology requires positions, matching pin weights and triangle indices");
    glm::vec3 minimum = renderPositions.front(), maximum = minimum;
    for (const auto& p : renderPositions)
    {
        if (!finite(p)) throw std::invalid_argument("Non-finite cloth rest vertex");
        minimum = glm::min(minimum, p);
        maximum = glm::max(maximum, p);
    }
    auto topology = std::shared_ptr<ClothTopology>(new ClothTopology);
    const float weld = settings.seamWeldDistance == 0.0f ?
        (std::max)(glm::length(maximum - minimum) * 0.000001f, 0.0000001f) :
        settings.seamWeldDistance;
    std::unordered_map<Cell, std::vector<std::uint32_t>, CellHash> buckets;
    topology->m_renderToParticle.reserve(renderPositions.size());
    for (std::size_t i = 0; i < renderPositions.size(); ++i)
    {
        std::uint32_t match = std::numeric_limits<std::uint32_t>::max();
        Cell key{};
        if (std::isfinite(weld) && weld > 0.0f)
        {
            key = cell(renderPositions[i], weld);
            for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                {
                    const auto found = buckets.find({key.x + x, key.y + y, key.z + z});
                    if (found == buckets.end()) continue;
                    for (const auto candidate : found->second)
                    {
                        const glm::vec3 delta = topology->m_restPositions[candidate] - renderPositions[i];
                        if (glm::dot(delta, delta) <= weld * weld && candidate < match) match = candidate;
                    }
                }
        }
        const float pin = std::clamp(nonnegative(renderPins[i], 0.0f), 0.0f, 1.0f);
        if (match == std::numeric_limits<std::uint32_t>::max())
        {
            match = std::uint32_t(topology->m_restPositions.size());
            topology->m_restPositions.push_back(renderPositions[i]);
            topology->m_pinWeights.push_back(pin);
            if (weld > 0.0f) buckets[key].push_back(match);
        }
        else topology->m_pinWeights[match] = (std::max)(pin, topology->m_pinWeights[match]);
        topology->m_renderToParticle.push_back(match);
    }
    topology->m_areas.resize(topology->particleCount(), 0.0f);
    topology->m_collisionNeighbours.resize(topology->particleCount());
    struct EdgeTriangles
    {
        std::uint32_t a, b, c, d = 0;
        unsigned count = 1;
        bool opposite = false;
    };
    std::unordered_map<std::uint64_t, EdgeTriangles> edgeTriangles;
    std::unordered_set<TriangleKey, TriangleHash> uniqueTriangles;
    for (std::size_t i = 0; i < renderTriangles.size(); i += 3)
    {
        std::array<std::uint32_t, 3> v;
        for (unsigned j = 0; j < 3; ++j)
        {
            if (renderTriangles[i + j] >= renderPositions.size())
                throw std::invalid_argument("Cloth triangle index is out of bounds");
            v[j] = topology->m_renderToParticle[renderTriangles[i + j]];
        }
        if (v[0] == v[1] || v[0] == v[2] || v[1] == v[2]) continue;
        const float area = glm::length(glm::cross(topology->m_restPositions[v[1]] - topology->m_restPositions[v[0]],
            topology->m_restPositions[v[2]] - topology->m_restPositions[v[0]])) * 0.5f;
        if (!(area > 1.0e-12f) || !std::isfinite(area)) continue;
        TriangleKey canonical{v};
        std::sort(canonical.vertices.begin(), canonical.vertices.end());
        if (!uniqueTriangles.insert(canonical).second) continue;
        topology->m_triangles.insert(topology->m_triangles.end(), v.begin(), v.end());
        for (unsigned j = 0; j < 3; ++j)
        {
            topology->m_areas[v[j]] += area / 3.0f;
            const std::uint32_t a = v[j], b = v[(j + 1) % 3], c = v[(j + 2) % 3];
            topology->m_collisionNeighbours[a].push_back(b);
            topology->m_collisionNeighbours[b].push_back(a);
            const auto key = edgeKey(a, b);
            const auto found = edgeTriangles.find(key);
            if (found == edgeTriangles.end()) edgeTriangles.emplace(key, EdgeTriangles{a, b, c});
            else
            {
                found->second.d = c;
                found->second.opposite = a == found->second.b && b == found->second.a;
                ++found->second.count;
            }
        }
    }
    // Sort edges for deterministic iteration; hash bucket order never affects
    // the simulation or its results across repeated topology builds.
    std::vector<std::uint64_t> edgeOrder;
    edgeOrder.reserve(edgeTriangles.size());
    for (const auto& e : edgeTriangles) edgeOrder.push_back(e.first);
    std::sort(edgeOrder.begin(), edgeOrder.end());
    for (const auto key : edgeOrder)
    {
        const auto& e = edgeTriangles.at(key);
        const float length = glm::length(topology->m_restPositions[e.a] - topology->m_restPositions[e.b]);
        topology->m_edges.push_back({e.a, e.b, length});
        float angle = 0.0f;
        if (e.count == 2 && e.opposite && dihedral(topology->m_restPositions[e.a],
            topology->m_restPositions[e.b], topology->m_restPositions[e.c], topology->m_restPositions[e.d], angle))
            topology->m_bends.push_back({e.a, e.b, e.c, e.d, angle, length});
    }
    const auto firstRing = topology->m_collisionNeighbours;
    for (std::size_t i = 0; i < firstRing.size(); ++i)
    {
        auto& neighbours = topology->m_collisionNeighbours[i];
        for (const auto adjacent : firstRing[i])
            neighbours.insert(neighbours.end(), firstRing[adjacent].begin(), firstRing[adjacent].end());
        neighbours.push_back(std::uint32_t(i));
        std::sort(neighbours.begin(), neighbours.end());
        neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
    }
    if (!topology->valid()) throw std::invalid_argument("Cloth mesh contains no non-degenerate triangles");
    if (settings.maxParticles && topology->particleCount() > (std::max)(settings.maxParticles, std::size_t(32)))
    {
        const auto count = topology->particleCount();
        std::vector<std::uint32_t> parents(count), coarseMap(count);
        const auto find = [&parents](std::uint32_t i)
        {
            while (parents[i] != i)
            {
                parents[i] = parents[parents[i]];
                i = parents[i];
            }
            return i;
        };
        const auto pinClass = [&topology](std::uint32_t i)
        {
            const float pin = topology->m_pinWeights[i];
            return pin >= HardPin ? 2 : pin > 0.0f ? 1 : 0;
        };
        // Merge adjacent material vertices only. Spatially close front/back
        // folds and disconnected layers are never accidentally stitched.
        float spacing = (std::max)(glm::length(maximum - minimum) * 0.005f, 0.000001f);
        std::size_t coarseCount = count;
        for (unsigned attempt = 0; attempt < 40; ++attempt)
        {
            for (std::size_t i = 0; i < count; ++i) parents[i] = std::uint32_t(i);
            std::vector<Cell> cells;
            cells.reserve(count);
            for (const auto& p : topology->m_restPositions) cells.push_back(cell(p - minimum, spacing));
            for (const auto& edge : topology->m_edges)
            {
                if (!(cells[edge.a] == cells[edge.b]) || pinClass(edge.a) != pinClass(edge.b)) continue;
                auto a = find(edge.a), b = find(edge.b);
                if (a > b) std::swap(a, b);
                parents[b] = a;
            }
            std::unordered_map<std::uint32_t, std::uint32_t> indices;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto root = find(std::uint32_t(i));
                const auto inserted = indices.emplace(root, std::uint32_t(indices.size()));
                coarseMap[i] = inserted.first->second;
            }
            coarseCount = indices.size();
            if (coarseCount <= (std::max)(settings.maxParticles, std::size_t(32))) break;
            spacing *= 1.20f;
        }
        std::vector<glm::vec3> coarsePositions(coarseCount, glm::vec3(0.0f));
        std::vector<float> coarsePins(coarseCount, 0.0f), occurrences(coarseCount, 0.0f);
        // The renderer averages its original skin targets using this same map.
        // Averaging original render vertices here makes its bind pose exact,
        // including meshes with asymmetric UV seam duplication.
        for (std::size_t i = 0; i < renderPositions.size(); ++i)
        {
            const auto fine = topology->m_renderToParticle[i], coarse = coarseMap[fine];
            coarsePositions[coarse] += renderPositions[i];
            occurrences[coarse] += 1.0f;
            coarsePins[coarse] = (std::max)(coarsePins[coarse], topology->m_pinWeights[fine]);
        }
        for (std::size_t i = 0; i < coarseCount; ++i) coarsePositions[i] /= occurrences[i];
        std::vector<std::uint32_t> coarseTriangles;
        std::vector<bool> used(coarseCount, false);
        for (std::size_t i = 0; i < topology->m_triangles.size(); i += 3)
        {
            const std::array<std::uint32_t, 3> v{{coarseMap[topology->m_triangles[i]],
                coarseMap[topology->m_triangles[i + 1]], coarseMap[topology->m_triangles[i + 2]]}};
            if (v[0] == v[1] || v[0] == v[2] || v[1] == v[2]) continue;
            if (glm::length(glm::cross(coarsePositions[v[1]] - coarsePositions[v[0]],
                coarsePositions[v[2]] - coarsePositions[v[0]])) <= 1.0e-12f) continue;
            coarseTriangles.insert(coarseTriangles.end(), v.begin(), v.end());
            for (const auto index : v) used[index] = true;
        }
        if (!coarseTriangles.empty())
        {
            std::vector<std::uint32_t> compactMap(coarseCount, std::numeric_limits<std::uint32_t>::max());
            std::vector<glm::vec3> compactPositions;
            std::vector<float> compactPins;
            for (std::size_t i = 0; i < coarseCount; ++i)
                if (used[i])
                {
                    compactMap[i] = std::uint32_t(compactPositions.size());
                    compactPositions.push_back(coarsePositions[i]);
                    compactPins.push_back(coarsePins[i]);
                }
            for (std::size_t i = 0; i < coarseCount; ++i)
                if (!used[i])
                {
                    float closest = std::numeric_limits<float>::max();
                    for (std::size_t j = 0; j < compactPositions.size(); ++j)
                    {
                        const auto category = [](float pin)
                        { return pin >= HardPin ? 2 : pin > 0.0f ? 1 : 0; };
                        if (category(compactPins[j]) != category(coarsePins[i])) continue;
                        // Isolated slivers inherit a nearby proxy point. Their
                        // full rendered shape is still retained by embedding.
                        const float distance = glm::length(compactPositions[j] - coarsePositions[i]);
                        if (distance < closest)
                        { closest = distance; compactMap[i] = std::uint32_t(j); }
                    }
                    // A tiny disconnected material component can collapse to
                    // an orphan cluster. Retain it if its pin category has no
                    // surviving triangle, rather than changing its attachment.
                    if (compactMap[i] == std::numeric_limits<std::uint32_t>::max())
                    {
                        compactMap[i] = std::uint32_t(compactPositions.size());
                        compactPositions.push_back(coarsePositions[i]);
                        compactPins.push_back(coarsePins[i]);
                    }
                }
            std::vector<glm::vec3> means(compactPositions.size(), glm::vec3(0));
            std::vector<unsigned> totals(compactPositions.size(), 0);
            for (std::size_t i = 0; i < renderPositions.size(); ++i)
            {
                const auto destination = compactMap[coarseMap[topology->m_renderToParticle[i]]];
                means[destination] += renderPositions[i];
                ++totals[destination];
            }
            for (std::size_t i = 0; i < compactPositions.size(); ++i)
                if (totals[i]) compactPositions[i] = means[i] / float(totals[i]);
            for (auto& index : coarseTriangles) index = compactMap[index];
            ClothBuildSettings proxySettings;
            proxySettings.seamWeldDistance = -1.0f;
            proxySettings.maxParticles = 0;
            auto proxy = std::const_pointer_cast<ClothTopology>(ClothTopology::build(compactPositions,
                coarseTriangles, compactPins, proxySettings));
            proxy->m_renderToParticle.resize(renderPositions.size());
            for (std::size_t i = 0; i < renderPositions.size(); ++i)
                proxy->m_renderToParticle[i] = compactMap[coarseMap[topology->m_renderToParticle[i]]];
            topology = std::move(proxy);
        }
    }
    // Embedding is restricted to triangles incident to the original vertex's
    // proxy cluster, preserving the material side of overlapping folds. Full
    // T/B/N offsets preserve the exact fine rest surface at mesh boundaries.
    std::vector<std::vector<std::uint32_t>> incident(topology->particleCount());
    for (std::size_t i = 0; i < topology->m_triangles.size(); i += 3)
        for (unsigned j = 0; j < 3; ++j) incident[topology->m_triangles[i + j]].push_back(std::uint32_t(i));
    topology->m_renderEmbedding.resize(renderPositions.size());
    for (std::size_t i = 0; i < renderPositions.size(); ++i)
    {
        auto& embedding = topology->m_renderEmbedding[i];
        float closest = std::numeric_limits<float>::max();
        const auto& candidates = incident[topology->m_renderToParticle[i]];
        const auto inspect = [&](std::uint32_t triangle)
        {
            const std::array<std::uint32_t, 3> v{{topology->m_triangles[triangle],
                topology->m_triangles[triangle + 1], topology->m_triangles[triangle + 2]}};
            const auto& a = topology->m_restPositions[v[0]];
            const auto& b = topology->m_restPositions[v[1]];
            const auto& c = topology->m_restPositions[v[2]];
            const glm::vec3 bary = closestTriangle(renderPositions[i], a, b, c);
            const glm::vec3 offset = renderPositions[i] - (a * bary.x + b * bary.y + c * bary.z);
            const float distance = glm::dot(offset, offset);
            if (distance >= closest) return;
            const glm::vec3 tangent = glm::normalize(b - a);
            const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const glm::vec3 bitangent = glm::cross(normal, tangent);
            closest = distance;
            embedding.particles = v;
            embedding.barycentric = bary;
            embedding.frameOffset = {glm::dot(offset, tangent), glm::dot(offset, bitangent), glm::dot(offset, normal)};
        };
        if (!candidates.empty()) for (const auto triangle : candidates) inspect(triangle);
        else for (std::size_t j = 0; j < topology->m_triangles.size(); j += 3) inspect(std::uint32_t(j));
    }
    // Multi-source material-space geodesics give every movable point a long
    // range tether to a real hard anchor. Shortcuts through empty world space
    // would incorrectly restrict folds, so lengths follow mesh edges instead.
    struct TetherQueueNode
    {
        float distance;
        std::uint32_t particle, anchor;
        bool operator>(const TetherQueueNode& other) const
        {
            if (distance != other.distance) return distance > other.distance;
            if (anchor != other.anchor) return anchor > other.anchor;
            return particle > other.particle;
        }
    };
    const auto count = topology->particleCount();
    std::vector<std::vector<std::pair<std::uint32_t, float>>> graph(count);
    for (const auto& edge : topology->m_edges)
    {
        graph[edge.a].emplace_back(edge.b, edge.length);
        graph[edge.b].emplace_back(edge.a, edge.length);
    }
    std::vector<float> distance(count, std::numeric_limits<float>::infinity());
    std::vector<std::uint32_t> anchor(count, std::numeric_limits<std::uint32_t>::max());
    std::priority_queue<TetherQueueNode, std::vector<TetherQueueNode>, std::greater<TetherQueueNode>> queue;
    for (std::size_t i = 0; i < count; ++i)
        if (topology->m_pinWeights[i] >= HardPin)
        {
            distance[i] = 0.0f;
            anchor[i] = std::uint32_t(i);
            queue.push({0.0f, std::uint32_t(i), std::uint32_t(i)});
        }
    while (!queue.empty())
    {
        const auto current = queue.top();
        queue.pop();
        if (current.distance != distance[current.particle] || current.anchor != anchor[current.particle]) continue;
        for (const auto& adjacent : graph[current.particle])
        {
            const float candidate = current.distance + adjacent.second;
            if (candidate > distance[adjacent.first] ||
                (candidate == distance[adjacent.first] && current.anchor >= anchor[adjacent.first])) continue;
            distance[adjacent.first] = candidate;
            anchor[adjacent.first] = current.anchor;
            queue.push({candidate, adjacent.first, current.anchor});
        }
    }
    topology->m_tethers.clear();
    for (std::size_t i = 0; i < count; ++i)
        if (topology->m_pinWeights[i] < HardPin && std::isfinite(distance[i]))
            topology->m_tethers.push_back({std::uint32_t(i), anchor[i], distance[i]});
    return topology;
}

ClothSimulation::ClothSimulation(std::shared_ptr<const ClothTopology> topology, const ClothSettings& settings)
    : m_topology(std::move(topology)), m_settings(settings)
{
    if (!m_topology || !m_topology->valid()) throw std::invalid_argument("Cloth simulation requires a valid topology");
    m_settings.fixedSeconds = std::clamp(positive(settings.fixedSeconds, 1.0f / 120.0f), 1.0f / 480.0f, 1.0f / 30.0f);
    m_settings.maxSubsteps = std::clamp(settings.maxSubsteps, 1u, 32u);
    m_settings.iterations = std::clamp(settings.iterations, 1u, 32u);
    m_settings.density = positive(settings.density, 0.20f);
    m_settings.damping = nonnegative(settings.damping, 2.0f);
    m_settings.stretchCompliance = nonnegative(settings.stretchCompliance, 0.0000002f);
    m_settings.bendCompliance = nonnegative(settings.bendCompliance, 0.002f);
    m_settings.attachmentCompliance = nonnegative(settings.attachmentCompliance, 0.0002f);
    m_settings.collisionThickness = nonnegative(settings.collisionThickness, 0.018f);
    m_settings.maxSpeed = positive(settings.maxSpeed, 60.0f);
    m_settings.selfCollisionCandidates = std::clamp(settings.selfCollisionCandidates, 8u, 256u);
    m_settings.tetherSlack = std::clamp(positive(settings.tetherSlack, 1.03f), 1.0f, 2.0f);
    if (!finite(m_settings.gravity)) m_settings.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    const auto count = m_topology->particleCount();
    m_inverseMass.resize(count);
    m_pinLambda.resize(count);
    m_contactNormal.resize(count);
    m_surfaceVelocity.resize(count);
    m_contactFriction.resize(count);
    m_contactResponse.resize(count);
    m_edgeLambda.resize(m_topology->m_edges.size());
    m_bendLambda.resize(m_topology->m_bends.size());
    m_bendRestWeight.resize(m_topology->m_bends.size());
    // At most one occupied cell per point. A half-full, power-of-two table
    // keeps lookup bounded without rebuilding maps or particle vectors.
    std::size_t selfGridSize = 1;
    while (selfGridSize < count * 2) selfGridSize *= 2;
    m_selfCollisionCells.resize(selfGridSize);
    m_selfCollisionNext.resize(count);
    prepareFixedCoefficients();
}

void ClothSimulation::prepareFixedCoefficients()
{
    const float dt = m_settings.fixedSeconds;
    m_dampingStep = std::exp(-m_settings.damping * dt);
    m_inverseDt2 = 1.0f / (dt * dt);
    m_gravityStep = m_settings.gravity * dt;
    m_stretchAlpha = m_settings.stretchCompliance * m_inverseDt2;
}

void ClothSimulation::setFixedSeconds(float seconds)
{
    if (!(seconds > 0.0f) || !std::isfinite(seconds)) return;
    seconds = std::clamp(seconds, 1.0f / 480.0f, 1.0f / 30.0f);
    if (seconds == m_settings.fixedSeconds) return;
    m_settings.fixedSeconds = seconds;
    // Preserve elapsed fractional time even when switching from a slow to a
    // fast tick. advance() still bounds catch-up and drops excess after work.
    prepareFixedCoefficients();
    if (m_ready) prepareConstraints();
}

void ClothSimulation::prepareConstraints()
{
    m_thickness = m_settings.collisionThickness * m_scale;
    m_maximumSpeed = m_settings.maxSpeed * m_scale;
    m_contactTolerance = 0.000002f * m_scale;
    m_preparedEdges.clear();
    m_preparedEdges.reserve(m_topology->m_edges.size());
    for (std::size_t i = 0; i < m_topology->m_edges.size(); ++i)
    {
        const auto& edge = m_topology->m_edges[i];
        const float weight = m_inverseMass[edge.a] + m_inverseMass[edge.b];
        if (weight == 0.0f) continue;
        m_preparedEdges.push_back({edge.a, edge.b, std::uint32_t(i),
            edge.length * m_scale, weight + m_stretchAlpha});
    }
    m_preparedBends.clear();
    m_preparedBends.reserve(m_topology->m_bends.size());
    for (std::size_t i = 0; i < m_topology->m_bends.size(); ++i)
    {
        const auto& bend = m_topology->m_bends[i];
        if (m_inverseMass[bend.a] + m_inverseMass[bend.b] +
            m_inverseMass[bend.c] + m_inverseMass[bend.d] == 0.0f) continue;
        const float maximumMove = bend.length * m_scale * 0.25f;
        // A generous rounding margin preserves the original float clamp path
        // near its threshold. Far below it, four square roots are unnecessary.
        const double safeMaximumMoveSquared = double(maximumMove) * double(maximumMove) *
            (1.0 - 32.0 * double(std::numeric_limits<float>::epsilon()));
        m_preparedBends.push_back({{{bend.a, bend.b, bend.c, bend.d}}, std::uint32_t(i),
            m_settings.bendCompliance * m_inverseDt2 * m_bendRestWeight[i],
            bend.angle * m_orientationParity, maximumMove, safeMaximumMoveSquared});
    }
    m_preparedPins.clear();
    m_preparedPins.reserve(m_positions.size());
    m_sampledPins.clear();
    m_hardPins.clear();
    m_dynamicParticles.clear();
    for (std::size_t i = 0; i < m_positions.size(); ++i)
    {
        const float pin = m_topology->m_pinWeights[i];
        if (pin > 0.0f) m_sampledPins.push_back(std::uint32_t(i));
        if (pin >= HardPin) m_hardPins.push_back(std::uint32_t(i));
        else
        {
            m_dynamicParticles.push_back(std::uint32_t(i));
            if (pin > 0.0f)
            {
                const float relative = (1.0f - pin) / pin;
                const float alpha = m_settings.attachmentCompliance * m_inverseMass[i] *
                    relative * relative * m_inverseDt2;
                m_preparedPins.push_back({std::uint32_t(i), alpha, m_inverseMass[i] + alpha});
            }
        }
    }
    m_preparedTethers.clear();
    m_preparedTethers.reserve(m_topology->m_tethers.size());
    for (const auto& tether : m_topology->m_tethers)
        m_preparedTethers.push_back({tether.particle, tether.anchor,
            tether.length * m_scale * m_settings.tetherSlack});
}

void ClothSimulation::preparePlanes(const std::vector<ClothPlane>& planes)
{
    m_planeStepCache.clear();
    m_planeStepCache.reserve(planes.size());
    for (const auto& plane : planes)
    {
        const float length = glm::length(plane.normal);
        if (!finite(plane.normal) || length < 1.0e-10f || !std::isfinite(plane.offset)) continue;
        const glm::vec3 normal = plane.normal / length;
        m_planeStepCache.push_back({{
            glm::vec4(plane.normal, length),
            glm::vec4(normal, plane.offset / length),
            glm::vec4(plane.offset, std::clamp(nonnegative(plane.friction, 0.0f), 0.0f, 1.0f),
                plane.offset + (m_thickness - m_contactTolerance) * length, 0.0f)}});
    }
}

void ClothSimulation::reset(const std::vector<glm::vec3>& targets, float scale, bool mirrored)
{
    if (targets.size() != m_topology->particleCount() ||
        std::any_of(targets.begin(), targets.end(), [](const glm::vec3& p) { return !finite(p); }))
        throw std::invalid_argument("Cloth reset requires one finite world target per particle");
    m_scale = positive(scale, 1.0f);
    m_orientationParity = mirrored ? -1.0f : 1.0f;
    m_positions = m_previous = m_rendered = m_frameTargets = m_stepTargets = targets;
    m_velocities.assign(targets.size(), glm::vec3(0.0f));
    m_frameCapsules.clear();
    m_simCapsules.clear();
    m_accumulator = 0.0f;
    m_lastSubsteps = 0;
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        const float area = (std::max)(m_topology->m_areas[i] * m_scale * m_scale, 0.0000001f);
        m_inverseMass[i] = m_topology->m_pinWeights[i] >= HardPin ? 0.0f :
            1.0f / (m_settings.density * area);
    }
    for (std::size_t j = 0; j < m_topology->m_bends.size(); ++j)
    {
        const auto& bend = m_topology->m_bends[j];
        const std::array<std::uint32_t, 4> index{{bend.a, bend.b, bend.c, bend.d}};
        std::array<glm::vec3, 4> gradient;
        float angle = 0.0f, weight = 0.0f;
        if (dihedral(m_topology->m_restPositions[bend.a], m_topology->m_restPositions[bend.b],
            m_topology->m_restPositions[bend.c], m_topology->m_restPositions[bend.d], angle, &gradient))
            for (unsigned k = 0; k < 4; ++k)
                weight += m_inverseMass[index[k]] * glm::dot(gradient[k], gradient[k]) / (m_scale * m_scale);
        m_bendRestWeight[j] = weight;
    }
    prepareConstraints();
    prepareSelfCollision();
    m_ready = true;
}

void ClothSimulation::advance(float deltaSeconds, const std::vector<glm::vec3>& targets,
    const std::vector<ClothCapsule>& capsules, const std::vector<ClothPlane>& planes)
{
    m_lastSubsteps = 0;
    if (!(deltaSeconds > 0.0f) || !std::isfinite(deltaSeconds)) return;
    if (targets.size() != m_topology->particleCount())
        throw std::invalid_argument("Cloth advance target count does not match topology");
    if (!m_ready) reset(targets, m_scale);
    const float dt = (std::min)(deltaSeconds, m_settings.fixedSeconds * float(m_settings.maxSubsteps));
    const float before = m_accumulator;
    m_accumulator += dt;
    m_cleanedCapsules.clear();
    m_cleanedCapsules.reserve(capsules.size());
    for (const auto& c : capsules) m_cleanedCapsules.push_back(cleanCapsule(c));
    if (m_frameCapsules.size() != m_cleanedCapsules.size()) m_frameCapsules = m_cleanedCapsules;
    if (m_simCapsules.size() != m_cleanedCapsules.size()) m_simCapsules = m_frameCapsules;
    m_sampledCapsules.resize(m_cleanedCapsules.size());
    preparePlanes(planes);
    m_preparedContacts.preparePlanes(planes, m_thickness);
    while (m_accumulator + 0.0000001f >= m_settings.fixedSeconds &&
        m_lastSubsteps < m_settings.maxSubsteps)
    {
        const float time = m_settings.fixedSeconds * float(m_lastSubsteps + 1) - before;
        const float alpha = std::clamp(time / dt, 0.0f, 1.0f);
        // Unpinned input is intentionally not sampled. Its value is never an
        // animation force, a visual lerp, or an implicit reset.
        for (const auto i : m_sampledPins)
        {
            const glm::vec3 next = finite(targets[i]) ? targets[i] : m_frameTargets[i];
            m_stepTargets[i] = glm::mix(m_frameTargets[i], next, alpha);
        }
        for (std::size_t i = 0; i < m_sampledCapsules.size(); ++i)
            m_sampledCapsules[i] = mixCapsule(m_frameCapsules[i], m_cleanedCapsules[i], alpha);
        step(m_settings.fixedSeconds, m_stepTargets, m_sampledCapsules, planes);
        m_accumulator = (std::max)(0.0f, m_accumulator - m_settings.fixedSeconds);
        ++m_lastSubsteps;
    }
    // Drop excess elapsed time rather than carrying an unbounded backlog.
    if (m_accumulator >= m_settings.fixedSeconds)
        m_accumulator = std::fmod(m_accumulator, m_settings.fixedSeconds);
    for (const auto i : m_sampledPins)
        if (finite(targets[i])) m_frameTargets[i] = targets[i];
    m_frameCapsules.swap(m_cleanedCapsules);
}

void ClothSimulation::step(float dt, const std::vector<glm::vec3>& targets,
    const std::vector<ClothCapsule>& capsules, const std::vector<ClothPlane>&)
{
    m_previous = m_positions;
    const float thickness = m_thickness;
    if (m_settings.triangleContacts)
        m_triangleCapsuleFeasibility.assign((m_topology->m_triangles.size() / 3) * capsules.size(), 0);
    for (const auto i : m_hardPins) m_positions[i] = targets[i];
    for (const auto i : m_dynamicParticles)
    {
        m_velocities[i] = (m_velocities[i] + m_gravityStep) * m_dampingStep;
        m_positions[i] += m_velocities[i] * dt;
    }
    std::fill(m_edgeLambda.begin(), m_edgeLambda.end(), 0.0f);
    std::fill(m_bendLambda.begin(), m_bendLambda.end(), 0.0f);
    std::fill(m_pinLambda.begin(), m_pinLambda.end(), glm::vec3(0.0f));
    std::fill(m_contactNormal.begin(), m_contactNormal.end(), glm::vec3(0.0f));
    std::fill(m_surfaceVelocity.begin(), m_surfaceVelocity.end(), glm::vec3(0.0f));
    std::fill(m_contactFriction.begin(), m_contactFriction.end(), 0.0f);
    std::fill(m_contactResponse.begin(), m_contactResponse.end(), ClothContactResponse::Physical);
    m_capsuleBounds.resize(capsules.size());
    m_capsuleStepCache.resize(capsules.size());
    m_capsuleStartCache.resize(m_positions.size() * capsules.size());
    m_capsuleStartValid.assign(m_capsuleStartCache.size(), 0);
    for (std::size_t j = 0; j < capsules.size(); ++j)
    {
        const auto& before = m_simCapsules[j];
        const auto& after = capsules[j];
        const glm::vec3 beforeAxis = before.b - before.a;
        const glm::vec3 afterAxis = after.b - after.a;
        const glm::vec3 firstMotion = after.a - before.a;
        const float radiusDelta = after.radius - before.radius;
        const bool sphere = before.a == before.b && after.a == after.b;
        auto& cache = m_capsuleStepCache[j];
        cache[0] = glm::vec4(beforeAxis, glm::dot(beforeAxis, beforeAxis));
        cache[1] = glm::vec4(afterAxis, glm::dot(afterAxis, afterAxis));
        cache[2] = glm::vec4(
            sphere ? glm::length(firstMotion) :
                (std::max)(glm::length(firstMotion), glm::length(after.b - before.b)),
            std::abs(radiusDelta), after.radius + thickness,
            (std::max)(1.0e-6f, thickness * 0.001f));
        cache[3] = glm::vec4(0.0f);
        cache[4] = glm::vec4(0.0f);
        if (sphere)
        {
            cache[3] = glm::vec4(firstMotion, 1.0f);
            cache[4] = glm::vec4(firstMotion / dt, radiusDelta);
        }
        const glm::vec3 padding((std::max)(before.radius, after.radius) + thickness);
        m_capsuleBounds[j][0] = glm::min(glm::min(before.a, before.b),
            glm::min(after.a, after.b)) - padding;
        m_capsuleBounds[j][1] = glm::max(glm::max(before.a, before.b),
            glm::max(after.a, after.b)) + padding;
    }
    m_capsulePackedBounds.resize((capsules.size() + 3) / 4);
    for (auto& group : m_capsulePackedBounds)
    {
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            group[axis] = glm::vec4(std::numeric_limits<float>::infinity());
            group[axis + 3] = glm::vec4(-std::numeric_limits<float>::infinity());
        }
    }
    for (std::size_t j = 0; j < capsules.size(); ++j)
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            m_capsulePackedBounds[j / 4][axis][j % 4] = m_capsuleBounds[j][0][axis];
            m_capsulePackedBounds[j / 4][axis + 3][j % 4] = m_capsuleBounds[j][1][axis];
        }
    m_preparedContacts.prepareCapsules(capsules, thickness);
    for (unsigned iteration = 0; iteration < m_settings.iterations; ++iteration)
    {
        for (std::size_t j = 0; j < m_preparedEdges.size(); ++j)
        {
            const auto& edge = m_preparedEdges[iteration % 2 ? m_preparedEdges.size() - 1 - j : j];
            const auto constraint = edge.constraint;
            const glm::vec3 delta = m_positions[edge.a] - m_positions[edge.b];
            const float length = glm::length(delta);
            if (length < 1.0e-10f) continue;
            const float lambda = (-(length - edge.length) - m_stretchAlpha * m_edgeLambda[constraint]) /
                edge.denominator;
            m_edgeLambda[constraint] += lambda;
            const glm::vec3 correction = delta * (lambda / length);
            m_positions[edge.a] += correction * m_inverseMass[edge.a];
            m_positions[edge.b] -= correction * m_inverseMass[edge.b];
        }
        for (std::size_t j = 0; j < m_preparedBends.size(); ++j)
        {
            const auto& bend = m_preparedBends[iteration % 2 ? m_preparedBends.size() - 1 - j : j];
            const auto constraint = bend.constraint;
            const auto& index = bend.particles;
            std::array<glm::vec3, 4> gradient;
            float angle = 0.0f;
            if (!dihedral(m_positions[index[0]], m_positions[index[1]],
                m_positions[index[2]], m_positions[index[3]], angle, &gradient)) continue;
            float denominator = bend.alpha;
            std::array<float, 4> gradientSquared;
            for (unsigned k = 0; k < 4; ++k)
            {
                gradientSquared[k] = glm::dot(gradient[k], gradient[k]);
                denominator += m_inverseMass[index[k]] * gradientSquared[k];
            }
            if (denominator <= 1.0e-12f) continue;
            float lambda = (-wrapAngle(angle - bend.angle) -
                bend.alpha * m_bendLambda[constraint]) / denominator;
            // Gradients and their mass-weighted denominator still depend on
            // the latest positions; only immutable coefficients are cached.
            const double lambdaSquared = double(lambda) * double(lambda);
            double largestMoveSquared = 0.0;
            bool intermediatesSafe = true;
            constexpr double maximumFloat = double(std::numeric_limits<float>::max());
            constexpr double safeIntermediateSquared = maximumFloat * maximumFloat *
                (1.0 - 32.0 * double(std::numeric_limits<float>::epsilon()));
            constexpr double minimumFloat = double(std::numeric_limits<float>::min());
            constexpr double minimumIntermediateSquared = minimumFloat * minimumFloat;
            for (unsigned k = 0; k < 4; ++k)
            {
                const double mass = double(m_inverseMass[index[k]]);
                const double massGradientSquared = mass * mass * double(gradientSquared[k]);
                // Retain the original overflow behavior before multiplication
                // by lambda; the final double product alone is insufficient.
                intermediatesSafe = intermediatesSafe && massGradientSquared < safeIntermediateSquared &&
                    (massGradientSquared == 0.0 || massGradientSquared >= minimumIntermediateSquared);
                largestMoveSquared = (std::max)(largestMoveSquared,
                    massGradientSquared * lambdaSquared);
            }
            if (!intermediatesSafe || bend.maximumMove < minimumFloat || !std::isfinite(largestMoveSquared) ||
                !(largestMoveSquared < bend.safeMaximumMoveSquared))
            {
                float largestMove = 0.0f;
                for (unsigned k = 0; k < 4; ++k)
                    largestMove = (std::max)(largestMove,
                        m_inverseMass[index[k]] * std::sqrt(gradientSquared[k]) * std::abs(lambda));
                if (largestMove > bend.maximumMove) lambda *= bend.maximumMove / largestMove;
            }
            m_bendLambda[constraint] += lambda;
            for (unsigned k = 0; k < 4; ++k)
                m_positions[index[k]] += m_inverseMass[index[k]] * gradient[k] * lambda;
        }
        // Pin operations are independent per point, so hard and partial lists
        // can omit every unpinned point without changing projection ordering.
        for (const auto i : m_hardPins) m_positions[i] = targets[i];
        for (const auto& pin : m_preparedPins)
        {
            const auto i = pin.particle;
            const glm::vec3 lambda = (-(m_positions[i] - targets[i]) - pin.alpha * m_pinLambda[i]) /
                pin.denominator;
            m_pinLambda[i] += lambda;
            m_positions[i] += lambda * m_inverseMass[i];
        }
        if (m_settings.tethers)
            for (const auto& tether : m_preparedTethers)
            {
                const auto delta = m_positions[tether.particle] - m_positions[tether.anchor];
                const float length = glm::length(delta);
                if (length > tether.maximum && length > 1.0e-10f)
                    m_positions[tether.particle] -= delta * ((length - tether.maximum) / length);
            }
        if (m_settings.triangleContacts) solveTriangleContacts(dt, capsules);
        // Self collision uses a spatial grid once at the end of the structural
        // iterations. Immediate/two-ring neighbours are excluded, preventing
        // thickness from inflating or fighting a finely tessellated rest mesh.
        if (m_settings.selfCollision && iteration + 1 == m_settings.iterations) solveSelfCollision();
        for (const auto i : m_dynamicParticles)
        {
            glm::vec3 pointMinimum = glm::min(m_previous[i], m_positions[i]);
            glm::vec3 pointMaximum = glm::max(m_previous[i], m_positions[i]);
            glm::vec3 travel = m_positions[i] - m_previous[i];
            float travelLength = glm::length(travel);
            for (std::size_t group = 0; group < m_capsulePackedBounds.size(); ++group)
            {
                unsigned mask = capsuleBroadphaseMask(pointMinimum, pointMaximum, m_capsulePackedBounds[group]);
                for (unsigned lane = 0; lane < 4; ++lane)
                {
                    const std::size_t j = group * 4 + lane;
                    if (j >= capsules.size()) break;
                    if (!(mask & (1u << lane)) || capsules[j].radius <= 0.0f) continue;
                    glm::vec3 normal, velocity;
                    if (collideCapsule(m_previous[i], m_positions[i], m_simCapsules[j], capsules[j],
                        thickness, travel, travelLength, m_capsuleStepCache[j], dt,
                        m_capsuleStartCache[i * capsules.size() + j],
                        m_capsuleStartValid[i * capsules.size() + j], normal, velocity))
                    {
                        m_contactNormal[i] = normal;
                        m_surfaceVelocity[i] = velocity;
                        m_contactFriction[i] = capsules[j].friction;
                        m_contactResponse[i] = capsules[j].response;
                        pointMinimum = glm::min(m_previous[i], m_positions[i]);
                        pointMaximum = glm::max(m_previous[i], m_positions[i]);
                        travel = m_positions[i] - m_previous[i];
                        travelLength = glm::length(travel);
                        // A correction can reach a later capsule whose first
                        // mask was empty. Recheck remaining lanes in order.
                        mask = capsuleBroadphaseMask(pointMinimum, pointMaximum, m_capsulePackedBounds[group]);
                    }
                }
            }
            for (const auto& plane : m_planeStepCache)
            {
                const glm::vec3 normal(plane[1]);
                const float gap = glm::dot(normal, m_positions[i]) - plane[1].w - thickness;
                if (gap < 0.0f)
                {
                    m_positions[i] -= normal * gap;
                    m_contactNormal[i] = normal;
                    m_surfaceVelocity[i] = glm::vec3(0.0f);
                    m_contactFriction[i] = plane[2].y;
                    m_contactResponse[i] = ClothContactResponse::Physical;
                }
            }
        }
    }
    // The CCD pass transports points with moving surfaces. After the final
    // structural projection, resolve the union of the current body capsules
    // without re-sweeping from the old point: another overlapping capsule may
    // otherwise have pushed it back inside a capsule visited earlier.
    for (const auto i : m_dynamicParticles)
    {
        for (unsigned pass = 0; pass < 12; ++pass)
        {
            bool corrected = false;
            for (std::size_t j = 0; j < capsules.size(); ++j)
            {
                if (capsules[j].radius <= 0.0f) continue;
                const auto& bounds = m_capsuleBounds[j];
                const auto& p = m_positions[i];
                if (p.x < bounds[0].x || p.x > bounds[1].x || p.y < bounds[0].y ||
                    p.y > bounds[1].y || p.z < bounds[0].z || p.z > bounds[1].z) continue;
                float segment = 0.0f;
                const auto closest = capsulePointCached(p, capsules[j], m_capsuleStepCache[j][1], segment);
                const auto difference = p - closest;
                const float radius = m_capsuleStepCache[j][2].z;
                const float exterior = radius - 0.000001f * m_scale;
                if (m_capsuleStepCache[j][3].w != 0.0f && exterior > 0.0f &&
                    glm::dot(difference, difference) >= exterior * exterior) continue;
                const float distance = glm::length(difference);
                if (distance >= radius - 0.000001f * m_scale) continue;
                const auto normal = glm::dot(difference, difference) > 1.0e-16f ? difference / distance :
                    safeNormal(difference, m_previous[i] - closest, glm::vec3(m_capsuleStepCache[j][1]));
                m_positions[i] = closest + normal * radius;
                m_contactNormal[i] = normal;
                m_surfaceVelocity[i] = (capsules[j].a + glm::vec3(m_capsuleStepCache[j][1]) * segment -
                    m_simCapsules[j].a - glm::vec3(m_capsuleStepCache[j][0]) * segment) / dt;
                m_contactFriction[i] = capsules[j].friction;
                m_contactResponse[i] = capsules[j].response;
                corrected = true;
            }
            if (!corrected) break;
        }
        const float tolerance = m_contactTolerance;
        bool needsEscape = false;
        for (std::size_t j = 0; j < capsules.size(); ++j)
        {
            if (capsules[j].radius <= 0.0f) continue;
            const auto& bounds = m_capsuleBounds[j];
            const auto& p = m_positions[i];
            if (p.x < bounds[0].x || p.x > bounds[1].x || p.y < bounds[0].y ||
                p.y > bounds[1].y || p.z < bounds[0].z || p.z > bounds[1].z) continue;
            float segment = 0.0f;
            const auto difference = p - capsulePointCached(p, capsules[j], m_capsuleStepCache[j][1], segment);
            const float exterior = m_capsuleStepCache[j][2].z - tolerance;
            const bool inside = m_capsuleStepCache[j][3].w != 0.0f ?
                (exterior > 0.0f && glm::dot(difference, difference) < exterior * exterior) :
                glm::length(difference) < exterior;
            if (inside)
            { needsEscape = true; break; }
        }
        for (const auto& plane : m_planeStepCache)
        {
            if (plane[0].w > 1.0e-10f &&
                glm::dot(glm::vec3(plane[0]), m_positions[i]) < plane[2].z)
                needsEscape = true;
        }
        if (needsEscape)
        {
            const auto before = m_positions[i];
            m_positions[i] = m_preparedContacts.projectOutside(before, m_previous[i] - before);
            // Choose the actual final touching surface for velocity/friction.
            // An escape from a torso/forearm overlap must not inherit the last
            // forearm's velocity after it has landed on the torso instead.
            float nearestGap = std::numeric_limits<float>::max();
            for (std::size_t j = 0; j < capsules.size(); ++j)
            {
                if (capsules[j].radius <= 0.0f) continue;
                float segment = 0.0f;
                const auto closest = capsulePointCached(m_positions[i], capsules[j], m_capsuleStepCache[j][1], segment);
                const auto difference = m_positions[i] - closest;
                const float gap = std::abs(glm::length(difference) - capsules[j].radius - thickness);
                if (gap >= nearestGap) continue;
                nearestGap = gap;
                m_contactNormal[i] = safeNormal(difference, before - closest, glm::vec3(m_capsuleStepCache[j][1]));
                m_surfaceVelocity[i] = (closest - m_simCapsules[j].a -
                    glm::vec3(m_capsuleStepCache[j][0]) * segment) / dt;
                m_contactFriction[i] = capsules[j].friction;
                m_contactResponse[i] = capsules[j].response;
            }
            for (const auto& plane : m_planeStepCache)
            {
                const float gap = std::abs(glm::dot(glm::vec3(plane[0]), m_positions[i]) / plane[0].w -
                    plane[1].w - thickness);
                if (gap >= nearestGap) continue;
                nearestGap = gap;
                m_contactNormal[i] = glm::vec3(plane[1]);
                m_surfaceVelocity[i] = glm::vec3(0.0f);
                m_contactFriction[i] = plane[2].y;
                m_contactResponse[i] = ClothContactResponse::Physical;
            }
        }
    }
    const float maximumSpeed = m_maximumSpeed;
    for (std::size_t i = 0; i < m_positions.size(); ++i)
    {
        if (!finite(m_positions[i]))
        {
            // Recover locally from malformed collision inputs; never replace
            // a free particle with its animated skin pose.
            m_positions[i] = m_previous[i];
            m_velocities[i] = glm::vec3(0.0f);
            continue;
        }
        const glm::vec3 integratedVelocity = m_velocities[i];
        m_velocities[i] = (m_positions[i] - m_previous[i]) / dt;
        if (m_inverseMass[i] == 0.0f) continue;
        if (glm::dot(m_contactNormal[i], m_contactNormal[i]) > 0.0f)
        {
            if (m_contactResponse[i] == ClothContactResponse::SeparationOnly)
            {
                // This protective helper changes positions, not kinetic
                // energy. Structural/CCD corrections can tilt the final
                // contact normal, so clamping only reconstructed normal speed
                // would leave an artificial tangent launch. Keep the original
                // integrated inertia instead, removing only inward motion.
                const float normalSpeed = glm::dot(integratedVelocity, m_contactNormal[i]);
                m_velocities[i] = integratedVelocity - m_contactNormal[i] *
                    (std::min)(0.0f, normalSpeed);
            }
            else
            {
                glm::vec3 relative = m_velocities[i] - m_surfaceVelocity[i];
                const float normalSpeed = glm::dot(relative, m_contactNormal[i]);
                const glm::vec3 tangent = relative - normalSpeed * m_contactNormal[i];
                relative = tangent * (1.0f - m_contactFriction[i]) +
                    (std::max)(0.0f, normalSpeed) * m_contactNormal[i];
                m_velocities[i] = relative + m_surfaceVelocity[i];
            }
        }
        const float speed = glm::length(m_velocities[i]);
        if (speed > maximumSpeed) m_velocities[i] *= maximumSpeed / speed;
    }
    m_simCapsules = capsules;
}


void ClothSimulation::prepareSelfCollision()
{
    constexpr std::size_t maximumCachedParticles = 1200;
    const std::size_t count = m_positions.size();
    if (!m_settings.selfCollision || count > maximumCachedParticles)
    {
        m_selfCollisionExclusions.clear();
        m_selfPairOffsets.clear();
        m_selfSeparation.clear();
        m_selfExclusionRowWords = 0;
        return;
    }
    m_selfExclusionRowWords = (count + 63) / 64;
    m_selfCollisionExclusions.assign(count * m_selfExclusionRowWords, 0);
    for (std::size_t i = 0; i < count; ++i)
        for (const auto neighbour : m_topology->m_collisionNeighbours[i])
            m_selfCollisionExclusions[i * m_selfExclusionRowWords + neighbour / 64] |=
                std::uint64_t(1) << (neighbour % 64);
    m_selfPairOffsets.resize(count);
    m_selfSeparation.resize(count * (count - 1) / 2);
    const float distance = m_thickness * 2.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        m_selfPairOffsets[i] = i == 0 ? 0 : i * (i - 1) / 2;
        for (std::size_t j = 0; j < i; ++j)
        {
            const float restDistance = glm::length(m_topology->m_restPositions[i] -
                m_topology->m_restPositions[j]) * m_scale;
            m_selfSeparation[m_selfPairOffsets[i] + j] = (std::min)(distance, restDistance * 0.90f);
        }
    }
}

void ClothSimulation::solveSelfCollision()
{
    const float distance = m_settings.collisionThickness * m_scale * 2.0f;
    if (!(distance > 0.0f)) return;
    constexpr std::uint32_t noParticle = UINT32_MAX;
    const auto mask = m_selfCollisionCells.size() - 1;
    for (auto& entry : m_selfCollisionCells) entry.first = noParticle;
    const auto slotFor = [&](const Cell& key)
    {
        auto slot = CellHash{}(key) & mask;
        while (m_selfCollisionCells[slot].first != noParticle)
        {
            const auto& entry = m_selfCollisionCells[slot];
            if (entry.x == key.x && entry.y == key.y && entry.z == key.z) break;
            slot = (slot + 1) & mask;
        }
        return slot;
    };
    for (std::size_t i = 0; i < m_positions.size(); ++i)
    {
        if (!finite(m_positions[i])) continue;
        const Cell key = cell(m_positions[i], distance);
        auto& entry = m_selfCollisionCells[slotFor(key)];
        const auto particle = std::uint32_t(i);
        m_selfCollisionNext[particle] = noParticle;
        if (entry.first == noParticle)
        {
            entry.x = key.x; entry.y = key.y; entry.z = key.z;
            entry.first = particle;
        }
        else m_selfCollisionNext[entry.last] = particle;
        entry.last = particle;
    }
    for (std::size_t i = 0; i < m_positions.size(); ++i)
    {
        if (!finite(m_positions[i]) || m_inverseMass[i] == 0.0f) continue;
        const Cell key = cell(m_positions[i], distance);
        unsigned inspected = 0;
        for (int x = -1; x <= 1 && inspected < m_settings.selfCollisionCandidates; ++x)
            for (int y = -1; y <= 1 && inspected < m_settings.selfCollisionCandidates; ++y)
            for (int z = -1; z <= 1 && inspected < m_settings.selfCollisionCandidates; ++z)
            {
                const auto& entry = m_selfCollisionCells[slotFor({key.x + x, key.y + y, key.z + z})];
                for (auto j = entry.first; j != noParticle; j = m_selfCollisionNext[j])
                {
                    if (j == i || (m_inverseMass[j] > 0.0f && j < i)) continue;
                    if (!m_selfCollisionExclusions.empty())
                    {
                        if (m_selfCollisionExclusions[i * m_selfExclusionRowWords + j / 64] &
                            (std::uint64_t(1) << (j % 64))) continue;
                    }
                    else
                    {
                        const auto& neighbours = m_topology->m_collisionNeighbours[i];
                        if (std::binary_search(neighbours.begin(), neighbours.end(), j)) continue;
                    }
                    if (++inspected > m_settings.selfCollisionCandidates) break;
                    const float weight = m_inverseMass[i] + m_inverseMass[j];
                    if (weight == 0.0f) continue;
                    const glm::vec3 delta = m_positions[i] - m_positions[j];
                    const float length = glm::length(delta);
                    // Closed capes have two authored surfaces separated by
                    // less than the generic cloth thickness. Do not inflate
                    // this material shell merely because its opposite surface
                    // is outside the topological two-ring exclusion.
                    float separation;
                    if (!m_selfSeparation.empty())
                    {
                        const auto lower = (std::min)(i, std::size_t(j));
                        const auto upper = (std::max)(i, std::size_t(j));
                        separation = m_selfSeparation[m_selfPairOffsets[upper] + lower];
                    }
                    else
                    {
                        const float restDistance = glm::length(m_topology->m_restPositions[i] -
                            m_topology->m_restPositions[j]) * m_scale;
                        separation = (std::min)(distance, restDistance * 0.90f);
                    }
                    if (length >= separation) continue;
                    const glm::vec3 normal = safeNormal(delta,
                        m_topology->m_restPositions[i] - m_topology->m_restPositions[j], glm::vec3(0.0f));
                    const glm::vec3 correction = normal * ((separation - length) / weight);
                    m_positions[i] += correction * m_inverseMass[i];
                    m_positions[j] -= correction * m_inverseMass[j];
                }
            }
    }
}

const std::vector<glm::vec3>& ClothSimulation::renderPositions(const std::vector<glm::vec3>& targets)
{
    if (!m_ready) return m_rendered;
    if (targets.size() != m_positions.size())
        throw std::invalid_argument("Cloth draw target count does not match topology");
    // The skeleton and its body colliders are drawn at the current frame.
    // Interpolating from the previous physical sample would put cloth one
    // substep behind those moving surfaces, even with zero remaining time.
    const float predictionSeconds = std::clamp(m_accumulator, 0.0f, m_settings.fixedSeconds);
    for (std::size_t i = 0; i < m_positions.size(); ++i)
        m_rendered[i] = m_topology->m_pinWeights[i] >= HardPin && finite(targets[i]) ? targets[i] :
            m_positions[i] + m_velocities[i] * predictionSeconds;
    return m_rendered;
}

// Exact segment/triangle geometry for virtual cloth contact points.
// Coordinates are floats; local calculations use doubles for near-parallel
// segments and thin triangles, without allocations or iterative searches.
namespace
{
    struct ClothTriangleContact
    {
        glm::vec3 trianglePoint{0.0f}, segmentPoint{0.0f};
        glm::vec3 barycentric{1.0f, 0.0f, 0.0f};
        float segmentFraction = 0.0f;
        float distanceSquared = std::numeric_limits<float>::infinity();
    };

    bool clothTriangleFinite(const glm::vec3& p)
    { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

    // Both segments may collapse to a point. Returns the fractions along each.
    glm::dvec2 clothTriangleSegmentFractions(const glm::dvec3& p,
        const glm::dvec3& q, const glm::dvec3& a, const glm::dvec3& b)
    {
        const glm::dvec3 d1 = q - p, d2 = b - a, r = p - a;
        const double first2 = glm::dot(d1, d1), second2 = glm::dot(d2, d2);
        const double c = glm::dot(d1, r), f = glm::dot(d2, r);
        double s = 0.0, t = 0.0;
        if (first2 <= 1.0e-28)
            t = second2 > 1.0e-28 ? std::clamp(f / second2, 0.0, 1.0) : 0.0;
        else if (second2 <= 1.0e-28)
            s = std::clamp(-c / first2, 0.0, 1.0);
        else
        {
            const double cross = glm::dot(d1, d2);
            const double determinant = first2 * second2 - cross * cross;
            if (determinant > first2 * second2 * 1.0e-14)
                s = std::clamp((cross * f - c * second2) / determinant, 0.0, 1.0);
            t = (cross * s + f) / second2;
            if (t < 0.0) { t = 0.0; s = std::clamp(-c / first2, 0.0, 1.0); }
            else if (t > 1.0) { t = 1.0; s = std::clamp((cross - c) / first2, 0.0, 1.0); }
        }
        return {s, t};
    }

    glm::dvec3 clothTrianglePointBarycentric(const glm::dvec3& point,
        const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c)
    {
        const glm::dvec3 ab = b - a, ac = c - a, ap = point - a;
        const double d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
        if (d1 <= 0.0 && d2 <= 0.0) return {1, 0, 0};
        const glm::dvec3 bp = point - b;
        const double d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        if (d3 >= 0.0 && d4 <= d3) return {0, 1, 0};
        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
        { const double v = d1 / (d1 - d3); return {1.0 - v, v, 0}; }
        const glm::dvec3 cp = point - c;
        const double d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        if (d6 >= 0.0 && d5 <= d6) return {0, 0, 1};
        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
        { const double w = d2 / (d2 - d6); return {1.0 - w, 0, w}; }
        const double va = d3 * d6 - d5 * d4;
        if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0)
        { const double w = (d4 - d3) / (d4 - d3 + d5 - d6); return {0, 1.0 - w, w}; }
        const double denominator = va + vb + vc;
        if (denominator > 0.0)
        {
            const double v = vb / denominator, w = vc / denominator;
            return {1.0 - v - w, v, w};
        }
        // Caller uses segment/edge candidates for degenerate triangles.
        return {1, 0, 0};
    }

    ClothTriangleContact clothClosestSegmentTriangle(const glm::vec3& aFloat,
        const glm::vec3& bFloat, const glm::vec3& cFloat,
        const glm::vec3& capAFloat, const glm::vec3& capBFloat)
    {
        ClothTriangleContact result;
        if (!clothTriangleFinite(aFloat) || !clothTriangleFinite(bFloat) ||
            !clothTriangleFinite(cFloat) || !clothTriangleFinite(capAFloat) ||
            !clothTriangleFinite(capBFloat)) return result;
        const glm::dvec3 a(aFloat), b(bFloat), c(cFloat), p(capAFloat), q(capBFloat);
        const glm::dvec3 ab = b - a, ac = c - a, direction = q - p;
        const glm::dvec3 normal = glm::cross(ab, ac);
        const double normal2 = glm::dot(normal, normal);
        const double edgeScale2 = (std::max)(glm::dot(ab, ab), glm::dot(ac, ac));
        const bool triangleValid = normal2 > edgeScale2 * edgeScale2 * 1.0e-20 &&
            normal2 > 1.0e-56;
        double bestDistance2 = std::numeric_limits<double>::infinity();
        const auto candidate = [&](const glm::dvec3& barycentric, double fraction)
        {
            const glm::dvec3 triangle = a * barycentric.x + b * barycentric.y + c * barycentric.z;
            const glm::dvec3 segment = p + direction * fraction;
            const glm::dvec3 delta = triangle - segment;
            const double distance2 = glm::dot(delta, delta);
            if (distance2 >= bestDistance2) return;
            bestDistance2 = distance2;
            result.trianglePoint = glm::vec3(triangle);
            result.segmentPoint = glm::vec3(segment);
            result.barycentric = glm::vec3(barycentric);
            result.segmentFraction = float(fraction);
            result.distanceSquared = float(distance2);
        };
        if (triangleValid)
        {
            // A segment crossing the triangle interior is the absolute minimum.
            const double denominator = glm::dot(normal, direction);
            if (denominator != 0.0)
            {
                const double fraction = glm::dot(normal, a - p) / denominator;
                if (fraction >= 0.0 && fraction <= 1.0)
                {
                    const glm::dvec3 hit = p + direction * fraction;
                    const glm::dvec3 relative = hit - a;
                    const double d00 = glm::dot(ab, ab), d01 = glm::dot(ab, ac);
                    const double d11 = glm::dot(ac, ac), d20 = glm::dot(relative, ab);
                    const double d21 = glm::dot(relative, ac);
                    const double v = (d11 * d20 - d01 * d21) / normal2;
                    const double w = (d00 * d21 - d01 * d20) / normal2;
                    if (v >= -1.0e-12 && w >= -1.0e-12 && v + w <= 1.0 + 1.0e-12)
                    {
                        glm::dvec3 bary((std::max)(0.0, 1.0 - v - w),
                            (std::max)(0.0, v), (std::max)(0.0, w));
                        bary /= bary.x + bary.y + bary.z;
                        candidate(bary, fraction);
                        return result;
                    }
                }
            }
            // When disjoint, an endpoint or one triangle edge attains the min.
            candidate(clothTrianglePointBarycentric(p, a, b, c), 0.0);
            // A sphere has only one endpoint. For a valid triangle its exact
            // point/triangle minimum is already found; skip duplicate endpoint
            // and all three segment/edge searches. Degenerate faces retain
            // the general edge fallback below.
            if (capAFloat == capBFloat) return result;
            candidate(clothTrianglePointBarycentric(q, a, b, c), 1.0);
        }
        const glm::dvec2 first = clothTriangleSegmentFractions(p, q, a, b);
        candidate({1.0 - first.y, first.y, 0.0}, first.x);
        const glm::dvec2 second = clothTriangleSegmentFractions(p, q, b, c);
        candidate({0.0, 1.0 - second.y, second.y}, second.x);
        const glm::dvec2 third = clothTriangleSegmentFractions(p, q, c, a);
        candidate({third.y, 0.0, 1.0 - third.y}, third.x);
        return result;
    }
}

void ClothSimulation::solveTriangleContacts(float dt, const std::vector<ClothCapsule>& capsules)
{
    const float thickness = m_settings.collisionThickness * m_scale;
    const auto& triangles = m_topology->m_triangles;
    for (std::size_t face = 0; face < triangles.size(); face += 3)
    {
        const std::array<std::uint32_t, 3> index{{triangles[face], triangles[face + 1], triangles[face + 2]}};
        if (m_inverseMass[index[0]] + m_inverseMass[index[1]] + m_inverseMass[index[2]] == 0.0f) continue;
        const bool hasHard = m_inverseMass[index[0]] == 0.0f ||
            m_inverseMass[index[1]] == 0.0f || m_inverseMass[index[2]] == 0.0f;
        const auto& restA = m_topology->m_restPositions[index[0]];
        const auto& restB = m_topology->m_restPositions[index[1]];
        const auto& restC = m_topology->m_restPositions[index[2]];
        const float largestRestEdge = (std::max)({glm::length(restB - restA),
            glm::length(restC - restB), glm::length(restA - restC)}) * m_scale;
        auto& a = m_positions[index[0]];
        auto& b = m_positions[index[1]];
        auto& c = m_positions[index[2]];
        auto minimum = glm::min(glm::min(a, b), c);
        auto maximum = glm::max(glm::max(a, b), c);
        for (std::size_t j = 0; j < capsules.size(); ++j)
        {
            if (capsules[j].radius <= 0.0f) continue;
            const auto& bounds = m_capsuleBounds[j];
            if (maximum.x < bounds[0].x || minimum.x > bounds[1].x ||
                maximum.y < bounds[0].y || minimum.y > bounds[1].y ||
                maximum.z < bounds[0].z || minimum.z > bounds[1].z) continue;
            const float radius = capsules[j].radius + thickness;
            if (hasHard)
            {
                auto& feasibility = m_triangleCapsuleFeasibility[(face / 3) * capsules.size() + j];
                if (feasibility == 0)
                {
                    const float interiorRadius = (std::max)(0.0f, radius - 0.000001f * m_scale);
                    bool infeasible = false;
                    for (unsigned k = 0; k < 3 && !infeasible; ++k)
                    {
                        if (m_inverseMass[index[k]] != 0.0f) continue;
                        float segment = 0.0f;
                        const auto fixedPoint = m_positions[index[k]];
                        infeasible = glm::length(fixedPoint - capsulePoint(fixedPoint, capsules[j], segment)) < interiorRadius;
                        const unsigned next = (k + 1) % 3;
                        if (!infeasible && m_inverseMass[index[next]] == 0.0f)
                        {
                            const glm::dvec3 first(capsules[j].a), second(capsules[j].b);
                            const glm::dvec3 edgeFirst(fixedPoint), edgeSecond(m_positions[index[next]]);
                            const auto fraction = clothTriangleSegmentFractions(first, second, edgeFirst, edgeSecond);
                            const auto difference = first + (second - first) * fraction.x -
                                edgeFirst - (edgeSecond - edgeFirst) * fraction.y;
                            infeasible = glm::dot(difference, difference) < double(interiorRadius) * interiorRadius;
                        }
                    }
                    feasibility = infeasible ? 2 : 1;
                }
                if (feasibility == 2) continue;
            }
            const auto contact = clothClosestSegmentTriangle(a, b, c, capsules[j].a, capsules[j].b);
            if (!(contact.distanceSquared < radius * radius)) continue;
            const float distance = std::sqrt((std::max)(contact.distanceSquared, 0.0f));
            float denominator = 0.0f;
            glm::vec3 oldPoint(0.0f);
            for (unsigned k = 0; k < 3; ++k)
            {
                const float bary = contact.barycentric[k];
                denominator += m_inverseMass[index[k]] * bary * bary;
                oldPoint += m_previous[index[k]] * bary;
            }
            if (denominator <= 1.0e-12f) continue;
            glm::vec3 normal;
            const auto oldSurface = m_simCapsules[j].a +
                (m_simCapsules[j].b - m_simCapsules[j].a) * contact.segmentFraction;
            float oldSegment = 0.0f;
            const auto oldClosest = capsulePoint(oldPoint, m_simCapsules[j], oldSegment);
            const auto separation = contact.trianglePoint - contact.segmentPoint;
            if (glm::dot(separation, separation) > 1.0e-14f * m_scale * m_scale)
                normal = glm::normalize(separation);
            else if (glm::dot(oldPoint - oldClosest, oldPoint - oldClosest) > 1.0e-14f)
                normal = glm::normalize(oldPoint - oldClosest);
            else
            {
                const auto faceNormal = glm::cross(b - a, c - a);
                normal = safeNormal(faceNormal, m_previous[index[1]] - m_previous[index[0]],
                    capsules[j].b - capsules[j].a);
                const auto centre = (a + b + c) / 3.0f;
                if (glm::dot(centre - (capsules[j].a + capsules[j].b) * 0.5f, normal) < 0.0f)
                    normal = -normal;
            }
            float lambda = (radius - distance) / denominator;
            float maximumMove = 0.0f;
            for (unsigned k = 0; k < 3; ++k)
                maximumMove = (std::max)(maximumMove,
                    m_inverseMass[index[k]] * contact.barycentric[k] * lambda);
            // A contact beside two fixed vertices can have a very small
            // movable barycentric weight. Bound that projection to prevent an
            // impossible contact from launching its third vertex far away.
            if (maximumMove > largestRestEdge * 0.5f && maximumMove > 0.0f)
                lambda *= largestRestEdge * 0.5f / maximumMove;
            const auto surfaceVelocity = (contact.segmentPoint - oldSurface) / dt;
            for (unsigned k = 0; k < 3; ++k)
            {
                const float weight = m_inverseMass[index[k]] * contact.barycentric[k];
                if (weight <= 0.0f) continue;
                m_positions[index[k]] += normal * (weight * lambda);
                m_contactNormal[index[k]] = normal;
                m_surfaceVelocity[index[k]] = surfaceVelocity;
                m_contactFriction[index[k]] = capsules[j].friction;
                m_contactResponse[index[k]] = capsules[j].response;
            }
            minimum = glm::min(glm::min(a, b), c);
            maximum = glm::max(glm::max(a, b), c);
        }
    }
}


