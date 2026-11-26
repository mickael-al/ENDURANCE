#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace EnduranceEnvironment
{
    // Game-level air motion for ambient particles only. No dependency on the
    // atmosphere/cloud clock or wind settings, and no per-particle CPU work.
    inline glm::vec2 particleWind(glm::vec2 base, double seconds, float variation, float period)
    {
        const auto finite = [](float value) {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            return (bits & 0x7f800000u) != 0x7f800000u;
        };
        std::uint64_t timeBits;
        std::memcpy(&timeBits, &seconds, sizeof timeBits);
        if ((timeBits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) seconds = 0.0;
        seconds = std::clamp(seconds, -1.0e9, 1.0e9);
        base.x = finite(base.x) ? std::clamp(base.x, -100.0f, 100.0f) : 0.0f;
        base.y = finite(base.y) ? std::clamp(base.y, -100.0f, 100.0f) : 0.0f;
        variation = finite(variation) ? std::clamp(variation, 0.0f, 1.0f) : 0.65f;
        period = finite(period) ? std::clamp(period, 2.0f, 180.0f) : 24.0f;
        if (variation == 0.0f || glm::dot(base, base) == 0.0f) return base;
        const double phase = seconds * (6.283185307179586 / static_cast<double>(period));
        // Independent broad changes in heading and strength: smooth gusts,
        // without randomly reversing every frame or uniformly accelerating snow.
        const double angle = variation * (0.75 * std::sin(phase * 0.71) + 0.25 * std::sin(phase * 1.37));
        const float strength = static_cast<float>(1.0 + variation *
            (0.30 * std::sin(phase * 0.43) + 0.18 * std::sin(phase * 1.91)));
        const float c = static_cast<float>(std::cos(angle)), s = static_cast<float>(std::sin(angle));
        return glm::vec2(c * base.x - s * base.y, s * base.x + c * base.y) * strength;
    }
}
