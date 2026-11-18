#ifndef GE_SHADOW_CASTER_BOUNDS_HPP
#define GE_SHADOW_CASTER_BOUNDS_HPP

#include <glm/vec3.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace Ge
{
    // World-space bounds of geometry that is actually available for drawing.
    // A renderer must include all its casters, including those outside the main
    // camera frustum. Double coordinates keep projection arithmetic local.
    struct ShadowCasterBounds
    {
        glm::dvec3 minimum{0.0}, maximum{0.0};
    };

    // Opaque publication tokens also distinguish a restarted renderer at the
    // same address from its previous lifetime. Zero means no bounds provider.
    inline std::uint64_t nextShadowCasterBoundsRevision() noexcept
    {
        static std::atomic<std::uint64_t> revision{1};
        return revision.fetch_add(1, std::memory_order_relaxed);
    }

    inline bool finiteShadowCoordinate(double value) noexcept
    {
        // The engine uses -ffast-math; inspect IEEE-754 bits instead of relying
        // on std::isfinite, which that compiler option may fold to true.
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
    }

    inline bool validShadowCasterBounds(const ShadowCasterBounds& bounds) noexcept
    {
        for (int axis = 0; axis < 3; ++axis)
            if (!finiteShadowCoordinate(bounds.minimum[axis]) ||
                !finiteShadowCoordinate(bounds.maximum[axis]) ||
                bounds.minimum[axis] > bounds.maximum[axis]) return false;
        return true;
    }

    struct DirectionalShadowVolume
    {
        glm::dvec3 center{0.0};
        glm::dvec3 right{1.0, 0.0, 0.0}, up{0.0, 1.0, 0.0};
        glm::dvec3 direction{0.0, 0.0, -1.0}; // light propagation; orthonormal basis
        double radius = 0.0;
    };

    // The explicit margin remains a floor for renderers without CPU bounds.
    // Only extend light-space Z; XY coverage and cascade splits are unchanged.
    inline float fitDirectionalShadowCasterMargin(const DirectionalShadowVolume& volume,
        float baseMargin, const std::vector<ShadowCasterBounds>& bounds) noexcept
    {
        const double base = finiteShadowCoordinate(baseMargin) && baseMargin > 0.0f ? baseMargin : 0.0;
        if (!finiteShadowCoordinate(volume.radius) || volume.radius <= 0.0) return static_cast<float>(base);
        for (int axis = 0; axis < 3; ++axis)
            if (!finiteShadowCoordinate(volume.center[axis]) ||
                !finiteShadowCoordinate(volume.right[axis]) ||
                !finiteShadowCoordinate(volume.up[axis]) ||
                !finiteShadowCoordinate(volume.direction[axis])) return static_cast<float>(base);

        double required = base;
        const glm::dvec3 axes[] = {volume.right, volume.up, volume.direction};
        constexpr double roundoff = 8.0 * std::numeric_limits<float>::epsilon();
        for (const auto& box : bounds)
        {
            if (!validShadowCasterBounds(box)) continue;
            const glm::dvec3 center = 0.5 * box.minimum + 0.5 * box.maximum;
            const glm::dvec3 half = 0.5 * box.maximum - 0.5 * box.minimum;
            const glm::dvec3 relative = center - volume.center;
            const glm::dvec3 magnitude = glm::abs(center) + glm::abs(volume.center) + half;
            double minimumZ = 0.0, paddingZ = 0.0;
            bool relevant = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double position = glm::dot(axes[axis], relative);
                const double extent = glm::dot(glm::abs(axes[axis]), half);
                const double padding = roundoff * (glm::dot(glm::abs(axes[axis]), magnitude)
                    + volume.radius + 1.0);
                if (!finiteShadowCoordinate(position) || !finiteShadowCoordinate(extent) ||
                    !finiteShadowCoordinate(padding)) { relevant = false; break; }
                // Casters outside XY, or entirely downstream of the receivers,
                // cannot affect this cascade. Keep contacts and FP32 roundoff.
                if (position - extent > volume.radius + padding ||
                    (axis < 2 && position + extent < -volume.radius - padding))
                { relevant = false; break; }
                if (axis == 2) { minimumZ = position - extent; paddingZ = padding; }
            }
            if (relevant) required = (std::max)(required, -volume.radius - minimumZ + paddingZ);
        }
        // All engine geometry uses float matrices. The supported terrain domain
        // is far below this representable limit; never return infinity on input.
        required = (std::min)(required, static_cast<double>(std::numeric_limits<float>::max()) / 4.0);
        float result = static_cast<float>(required);
        if (static_cast<double>(result) < required)
            result = std::nextafter(result, std::numeric_limits<float>::infinity());
        return result;
    }

    // CPU-only aggregate owned by LightManager. Providers are queried for a
    // cheap revision every frame; their bounds are copied only on publication.
    class ShadowCasterBoundsCache
    {
    public:
        template<class Provider>
        bool update(const std::vector<Provider*>& providers)
        {
            bool changed = false;
            for (auto& source : m_sources) source.seen = false;
            for (const auto* provider : providers)
            {
                if (!provider) continue;
                const auto revision = provider->shadowCasterBoundsRevision();
                if (revision == 0) continue;
                auto found = std::find_if(m_sources.begin(), m_sources.end(),
                    [provider](const Source& source) { return source.identity == provider; });
                if (found == m_sources.end())
                {
                    m_sources.push_back({});
                    found = m_sources.end() - 1;
                    found->identity = provider;
                }
                found->seen = true;
                if (found->revision != revision)
                {
                    found->bounds.clear();
                    provider->appendShadowCasterBounds(found->bounds);
                    found->bounds.erase(std::remove_if(found->bounds.begin(), found->bounds.end(),
                        [](const ShadowCasterBounds& box) { return !validShadowCasterBounds(box); }), found->bounds.end());
                    found->revision = revision;
                    changed = true;
                }
            }
            const auto end = std::remove_if(m_sources.begin(), m_sources.end(),
                [](const Source& source) { return !source.seen; });
            if (end != m_sources.end()) { m_sources.erase(end, m_sources.end()); changed = true; }
            if (changed)
            {
                m_bounds.clear();
                std::size_t count = 0;
                for (const auto& source : m_sources) count += source.bounds.size();
                m_bounds.reserve(count);
                for (const auto& source : m_sources)
                    m_bounds.insert(m_bounds.end(), source.bounds.begin(), source.bounds.end());
                ++m_revision;
            }
            return changed;
        }

        const std::vector<ShadowCasterBounds>& bounds() const noexcept { return m_bounds; }
        std::uint64_t revision() const noexcept { return m_revision; }
        void clear() { m_sources.clear(); m_bounds.clear(); ++m_revision; }

    private:
        struct Source
        {
            const void* identity = nullptr;
            std::uint64_t revision = 0;
            std::vector<ShadowCasterBounds> bounds;
            bool seen = false;
        };
        std::vector<Source> m_sources;
        std::vector<ShadowCasterBounds> m_bounds;
        std::uint64_t m_revision = 0;
    };
}

#endif
