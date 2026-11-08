#ifndef ENDURANCE_PROCEDURAL_TERRAIN_FIELD_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_FIELD_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ProceduralTerrain
{
    enum class FieldMode : std::uint32_t
    {
        Terrain = 0,
        Plane = 1,
        Sphere = 2
    };

    // Keep these defaults and ranges in sync with world_field.glsl.
    // Every value is in world metres except the frequencies (cycles/metre).
    struct FieldParameters
    {
        FieldMode mode = FieldMode::Terrain;
        float baseHeight = 0.0f;
        float heightAmplitude = 320.0f;
        float heightFrequency = 0.00015f;
        float climateFrequency = 0.0005f;
        float bandOffset = 0.0f;
        float bandOffsetVariation = 0.0f;
        float detailScale = 0.35f;
        float seaLevel = 0.0f;
        float seaBlendWidth = 64.0f;
        // -1 = climate/altitude matrix; 0..24 = matrix row-major; 25 = marine.
        std::int32_t biomeOverride = -1;
        std::array<float, 3> sphereCenter{{0.0f, 0.0f, 0.0f}};
        float sphereRadius = 48.0f;
    };

    struct FieldHeightBounds
    {
        float minimumY;
        float maximumY;
    };

    // Analytic conservative bounds, not extrema estimated from chunk corners.
    // Biome height is clamped to [-640,640], total biome thickness to [0,576],
    // and the combined recipe offset to [-160,160], before detailScale.
    inline FieldHeightBounds fieldHeightBounds(const FieldParameters& p) noexcept
    {
        if (p.mode == FieldMode::Plane)
            return {p.baseHeight, p.baseHeight};
        if (p.mode == FieldMode::Sphere)
            return {p.sphereCenter[1] - p.sphereRadius, p.sphereCenter[1] + p.sphereRadius};

        const float minimumHeight = p.baseHeight - p.heightAmplitude - 640.0f * p.detailScale;
        const float maximumHeight = p.baseHeight + p.heightAmplitude + 640.0f * p.detailScale;
        const float halfThickness = 0.5f * (576.0f * p.detailScale);
        return {
            minimumHeight + (std::min)(0.0f, p.bandOffset - p.bandOffsetVariation - 160.0f * p.detailScale - halfThickness),
            maximumHeight + (std::max)(0.0f, p.bandOffset + p.bandOffsetVariation + 160.0f * p.detailScale + halfThickness)
        };
    }

    inline bool finiteFieldValue(float value) noexcept
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t) &&
            std::numeric_limits<float>::is_iec559, "Terrain fields require IEEE-754 float32");
        // The engine enables -ffast-math in Release; std::isfinite may then be
        // folded to true. Parameter validation must inspect the representation.
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    inline bool finiteFieldValue(double value) noexcept
    {
        static_assert(sizeof(double) == sizeof(std::uint64_t) &&
            std::numeric_limits<double>::is_iec559, "Terrain bounds require IEEE-754 float64");
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
    }

    inline bool validFieldParameters(const FieldParameters& p) noexcept
    {
        // Keep all distance intermediates comfortably below sqrt(FLT_MAX):
        // sphere length squares three differences; the terrain also divides
        // height differences by a half-thickness no smaller than 1e-4.
        constexpr float maximumMagnitude = 0x1p60f;
        const float values[] = {p.baseHeight, p.heightAmplitude, p.heightFrequency,
            p.climateFrequency, p.bandOffset,
            p.bandOffsetVariation, p.detailScale, p.seaLevel, p.seaBlendWidth, p.sphereCenter[0], p.sphereCenter[1],
            p.sphereCenter[2], p.sphereRadius};
        for (float v : values)
            if (!finiteFieldValue(v) || v < -maximumMagnitude || v > maximumMagnitude) return false;
        if (static_cast<std::uint32_t>(p.mode) > 2 || p.heightAmplitude < 0.0f ||
            p.heightFrequency < 0.0f || p.climateFrequency < 0.0f ||
            p.bandOffsetVariation < 0.0f || p.detailScale < 0.0f || p.sphereRadius <= 0.0f ||
            p.seaBlendWidth < 0.001f || p.biomeOverride < -1 || p.biomeOverride > 25)
            return false;
        const auto bounds = fieldHeightBounds(p);
        return finiteFieldValue(bounds.minimumY) && finiteFieldValue(bounds.maximumY) &&
            bounds.minimumY >= -maximumMagnitude && bounds.maximumY <= maximumMagnitude;
    }

    // Call before dispatch. The caller computes the maximum absolute world
    // coordinate of all three axes, including the complete [-1,N+1] halo,
    // in double precision after validating the integer lattice arithmetic.
    inline bool validFieldDomain(const FieldParameters& p, double maximumAbsoluteWorldCoordinate) noexcept
    {
        if (!validFieldParameters(p) || !finiteFieldValue(maximumAbsoluteWorldCoordinate) ||
            maximumAbsoluteWorldCoordinate < 0.0 || maximumAbsoluteWorldCoordinate > 0x1p60)
            return false;
        if (p.mode != FieldMode::Terrain) return true;

        // 0.25 conservatively bounds every fixed biome noise frequency and
        // coordinate transform in the shared recipes. The two
        // configurable frequencies must obey the same range. 2^30 leaves a
        // large margin for float rounding and the +/-1 noise-cell neighbours
        // before every GLSL float-to-int conversion.
        const double maximumFrequency = (std::max)(0.25,
            (std::max)(static_cast<double>(p.heightFrequency), static_cast<double>(p.climateFrequency)));
        return maximumAbsoluteWorldCoordinate * maximumFrequency <= 0x1p30;
    }
}

#endif
