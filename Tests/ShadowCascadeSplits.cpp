#include "ShadowCascadeSplits.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }

    template<std::size_t Count>
    void ordered(const std::array<float, Count>& splits)
    {
        float previous = 0.0f;
        for (float split : splits)
        {
            require(Ge::finiteShadowSplitValue(split), "cascade split is finite");
            require(split > previous && split <= 1.0f, "cascade split strictly ordered and bounded");
            previous = split;
        }
        require(splits.back() == 1.0f, "last cascade retains complete far coverage");
    }

    std::array<float, 4> oldSplits(float nearClip, float farClip)
    {
        std::array<float, 4> values{};
        const float clipRange = farClip - nearClip;
        const float minZ = nearClip, maxZ = nearClip + clipRange;
        const float range = maxZ - minZ, ratio = maxZ / minZ;
        for (unsigned i = 0; i < 4; ++i)
        {
            const float p = (i + 1) / 4.0f;
            const float logarithmic = minZ * std::pow(ratio, p);
            const float uniform = minZ + range * p;
            values[i] = (0.96f * (logarithmic - uniform) + uniform - nearClip) / clipRange;
        }
        return values;
    }
}

int main()
{
    try
    {
        for (const float farClip : {100.0f, 500.0f, 100000.0f})
        {
            const auto expected = oldSplits(0.04f, farClip);
            const auto normal = Ge::directionalCascadeSplits<4>(0.04f, farClip, 0.96f);
            ordered(normal);
            // Fast-math can reassociate the extracted helper by a few ULPs.
            for (unsigned i = 0; i < 3; ++i) require(std::abs(normal[i] - expected[i]) < 2.0e-7f,
                "default camera split behavior unchanged");
        }
        constexpr float nearClip = 0.04f, farClip = 100000.0f, range = farClip - nearClip;
        const auto actor = Ge::directionalCascadeSplits<4>(nearClip, farClip, 0.96f, 16.0f);
        ordered(actor);
        require(std::abs(nearClip + actor[0] * range - 16.0f) < 0.0001f, "first cascade ends at requested16m");
        float previousEnd = 16.0f;
        const double ratio = std::pow(double(farClip) / 16.0, 1.0 / 3.0);
        for (unsigned i = 1; i < 4; ++i)
        {
            const float end = nearClip + actor[i] * range;
            require(std::abs(end / previousEnd - ratio) < 0.0001, "remaining cascades are geometric");
            const double previousStart = i > 1 ? nearClip + actor[i - 2] * range : 0.0;
            const double overlapStart = nearClip + (std::max)(0.0,
                double(actor[i - 1]) - 0.10 * (previousEnd - previousStart) / range) * range;
            require(overlapStart < previousEnd && overlapStart >= nearClip, "conservative overlap remains present");
            require(std::abs(overlapStart - (previousEnd - 0.10 * (previousEnd - previousStart))) < 0.002,
                "overlap still matches10percent shader blend");
            previousEnd = end;
        }
        const auto normal = Ge::directionalCascadeSplits<4>(nearClip, farClip, 0.96f);
        for (float invalid : {0.0f, -1.0f, nearClip, farClip, farClip * 2.0f,
            (std::numeric_limits<float>::infinity)(), (std::numeric_limits<float>::quiet_NaN)()})
        {
            require(Ge::directionalCascadeSplits<4>(nearClip, farClip, 0.96f, invalid) == normal,
                "out-of-range override preserves default distribution");
        }
        require(Ge::sanitizeFirstShadowCascadeDistance(-1.0f) == 0.0f, "negative override disabled");
        require(Ge::sanitizeFirstShadowCascadeDistance((std::numeric_limits<float>::quiet_NaN)()) == 0.0f,
            "NaN override disabled under fast-math");
        ordered(Ge::directionalCascadeSplits<4>(nearClip, farClip, 0.96f, std::nextafter(farClip, 0.0f)));
        ordered(Ge::directionalCascadeSplits<4>(1.0f, 1.000001f, 0.96f, 1.0000005f));
        ordered(Ge::directionalCascadeSplits<4>(nearClip, (std::numeric_limits<float>::max)(), 0.96f, 16.0f));
        ordered(Ge::directionalCascadeSplits<4>(0.0f, 0.0f, 0.96f));
        ordered(Ge::directionalCascadeSplits<1>(nearClip, farClip, 0.96f, 16.0f));
        std::cout << "Directional cascade split tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
