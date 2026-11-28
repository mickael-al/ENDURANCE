#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Ge
{
    inline bool finiteShadowSplitValue(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    inline float sanitizeFirstShadowCascadeDistance(float metres)
    { return finiteShadowSplitValue(metres) && metres > 0.0f ? metres : 0.0f; }

    // Normalized distances in [near, far]. Zero/invalid firstEnd preserves the
    // existing practical split distribution. A camera may reserve cascade zero
    // for its actor without reducing the far range used by terrain/cloud shadows.
    template<std::size_t Count>
    std::array<float, Count> directionalCascadeSplits(float nearClip, float farClip,
        float lambda, float firstEnd = 0.0f)
    {
        static_assert(Count > 0, "Directional shadows require at least one cascade");
        std::array<float, Count> result{};
        // Malformed input gets a finite, strictly ordered fallback. Camera
        // projection validation remains the camera owner's responsibility.
        if (!finiteShadowSplitValue(nearClip) || !finiteShadowSplitValue(farClip) ||
            !(nearClip > 0.0f && farClip > nearClip))
        {
            for (std::size_t i = 0; i < Count; ++i) result[i] = float(i + 1) / float(Count);
            return result;
        }
        const float clipRange = farClip - nearClip;
        const float boundedLambda = finiteShadowSplitValue(lambda) ? std::clamp(lambda, 0.0f, 1.0f) : 0.96f;
        const bool closeCascade = Count > 1 && finiteShadowSplitValue(firstEnd) &&
            firstEnd > nearClip && firstEnd < farClip;
        if (closeCascade)
        {
            result[0] = (firstEnd - nearClip) / clipRange;
            for (std::size_t i = 1; i < Count; ++i)
            {
                const double p = double(i) / double(Count - 1);
                // Logs avoid ratio overflow at extreme but finite far planes.
                const double end = std::exp(std::log(double(firstEnd)) * (1.0 - p) +
                    std::log(double(farClip)) * p);
                result[i] = static_cast<float>((end - nearClip) / clipRange);
            }
        }
        else
        {
            // Keep the previous float operations for existing cameras.
            const float minZ = nearClip;
            const float maxZ = nearClip + clipRange;
            const float range = maxZ - minZ;
            const float ratio = maxZ / minZ;
            for (std::size_t i = 0; i < Count; ++i)
            {
                const float p = float(i + 1) / float(Count);
                const float logarithmic = minZ * std::pow(ratio, p);
                const float uniform = minZ + range * p;
                const float end = boundedLambda * (logarithmic - uniform) + uniform;
                result[i] = (end - nearClip) / clipRange;
            }
        }
        // Leave one representable value for each following split, even if a
        // first-end request lies within a few ULPs of the far plane.
        std::array<float, Count> upper{};
        upper[Count - 1] = 1.0f;
        for (std::size_t i = Count - 1; i > 0; --i) upper[i - 1] = std::nextafter(upper[i], 0.0f);
        float previous = 0.0f;
        for (std::size_t i = 0; i + 1 < Count; ++i)
        {
            const float candidate = finiteShadowSplitValue(result[i]) ? result[i] : float(i + 1) / float(Count);
            result[i] = std::clamp(candidate, std::nextafter(previous, 1.0f), upper[i]);
            previous = result[i];
        }
        result[Count - 1] = 1.0f;
        return result;
    }
}
