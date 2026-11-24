#ifndef ENDURANCE_PROCEDURAL_TERRAIN_MATERIALS_HPP
#define ENDURANCE_PROCEDURAL_TERRAIN_MATERIALS_HPP

#include "ProceduralTerrainBiomes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ProceduralTerrain
{
    // Fixed std140 records. Plain float arrays avoid a dependency on GLM's
    // optional aligned-gentype configuration. RGB is LINEAR reflectance, not
    // display-space color or emission. Each palette stop has its own roughness.
    using TerrainMaterialVector = std::array<float, 4>;

    struct alignas(16) TerrainMaterial
    {
        TerrainMaterialVector colorLow{{0.16f, 0.16f, 0.16f, 0.90f}};
        TerrainMaterialVector colorMid{{0.24f, 0.24f, 0.24f, 0.88f}};
        TerrainMaterialVector colorHigh{{0.32f, 0.32f, 0.32f, 0.86f}};
        // Metallic, material AO, normal strength, fine roughness variation.
        TerrainMaterialVector response{{0.0f, 1.0f, 1.0f, 0.04f}};
        // Middle stop, smooth/discrete blending, contrast, palette bias.
        TerrainMaterialVector ramp{{0.5f, 0.9f, 1.5f, 0.0f}};
        // Nonnegative macro/patch/strata/ripple weights, normalized in GLSL.
        // All zero is legal and selects the center of the palette.
        TerrainMaterialVector pattern{{0.65f, 0.35f, 0.0f, 0.0f}};
        // Sparkle strength, grain roughness, stain darkening, stain roughness
        // reduction. Per material, disabled in the generic engine defaults.
        // Darkening is a fraction of LINEAR albedo, not a display-space offset.
        TerrainMaterialVector fineSurface{{0.0f, 0.16f, 0.0f, 0.0f}};
    };

    struct alignas(16) TerrainSurface
    {
        TerrainMaterial base;
        TerrainMaterial accent;
        // Accent threshold, transition width, macro and patch mask weights.
        TerrainMaterialVector blend{{0.62f, 0.12f, 0.7f, 0.3f}};
        // Strata/ripple periods in metres, orientation radians, warp metres.
        TerrainMaterialVector detail{{24.0f, 1.5f, 0.0f, 2.0f}};
        // Independent normal family, feature period (m), warp fraction, fine-grain mix.
        // Families: grain, wind ribs, bedding, fractures, fibres, wet sediment,
        // ice veins, ropy lava. The palette does not determine surface relief.
        TerrainMaterialVector normal{{0.0f, 0.15f, 0.35f, 0.35f}};
    };

    struct alignas(16) TerrainBiomeAppearance
    {
        TerrainSurface ground;
        TerrainSurface wall;
        // Ground->wall slope interval (1-N.y), patch perturbation, normal gain.
        // The signed underlying normal is used, before material perturbation.
        TerrainMaterialVector slope{{0.28f, 0.65f, 0.07f, 1.0f}};
        // Patch-period multiplier, grain period (m), grain octaves, octave gain.
        // Each biome uses its own stable world-space domain before climate blending.
        TerrainMaterialVector spectrum{{1.0f, 0.10f, 1.0f, 0.32f}};
        // Large vegetation-stain weights: ground base/accent, wall base/accent.
        // Zero selects fine stains. Blend masks, never their spatial frequencies.
        TerrainMaterialVector stainRouting{{0.f, 0.f, 0.f, 0.f}};
    };

    using TerrainMaterialTable = std::array<TerrainBiomeAppearance, BiomeCount>;

    struct TerrainAppearance
    {
        TerrainMaterialTable biomes{};
        float macroPeriod = 384.0f;
        float patchPeriod = 32.0f;
        float vegetationStainSize = 2.5f;
        float otherStainSize = 0.25f;
        float sparkleGrainSize = 0.01f;
        int debugView = 0;
    };

    static_assert(sizeof(TerrainMaterialVector) == 16, "Terrain vec4 ABI");
    static_assert(std::is_standard_layout<TerrainMaterial>::value
        && std::is_trivially_copyable<TerrainMaterial>::value, "Terrain material upload ABI");
    static_assert(sizeof(TerrainMaterial) == 112 && alignof(TerrainMaterial) == 16,
        "TerrainMaterial must match seven std140 vec4s");
    static_assert(offsetof(TerrainMaterial, colorMid) == 16
        && offsetof(TerrainMaterial, colorHigh) == 32
        && offsetof(TerrainMaterial, response) == 48
        && offsetof(TerrainMaterial, ramp) == 64
        && offsetof(TerrainMaterial, pattern) == 80
        && offsetof(TerrainMaterial, fineSurface) == 96, "TerrainMaterial std140 offsets");
    static_assert(sizeof(TerrainSurface) == 272
        && offsetof(TerrainSurface, accent) == 112
        && offsetof(TerrainSurface, blend) == 224
        && offsetof(TerrainSurface, detail) == 240
        && offsetof(TerrainSurface, normal) == 256, "TerrainSurface std140 offsets");
    static_assert(sizeof(TerrainBiomeAppearance) == 592
        && offsetof(TerrainBiomeAppearance, wall) == 272
        && offsetof(TerrainBiomeAppearance, slope) == 544
        && offsetof(TerrainBiomeAppearance, spectrum) == 560
        && offsetof(TerrainBiomeAppearance, stainRouting) == 576, "TerrainBiomeAppearance std140 offsets");
    static_assert(sizeof(TerrainMaterialTable) == 15392
        && sizeof(TerrainMaterialTable) <= 16384, "Terrain table fits the minimum OpenGL UBO size");

    // Bit classification remains correct under a caller's -ffast-math build.
    inline bool terrainMaterialFinite(float value) noexcept
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "32-bit IEEE float required");
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    inline bool terrainMaterialRange(float value, float low, float high) noexcept
    {
        return terrainMaterialFinite(value) && value >= low && value <= high;
    }

    inline bool validTerrainMaterial(const TerrainMaterial& m) noexcept
    {
        for (const auto* stop : {&m.colorLow, &m.colorMid, &m.colorHigh})
        {
            for (unsigned int i = 0; i < 3; ++i)
                if (!terrainMaterialRange((*stop)[i], 0.0f, 1.0f)) return false;
            if (!terrainMaterialRange((*stop)[3], 0.04f, 1.0f)) return false;
        }
        if (!terrainMaterialRange(m.response[0], 0.0f, 1.0f)
            || !terrainMaterialRange(m.response[1], 0.0f, 1.0f)
            || !terrainMaterialRange(m.response[2], 0.0f, 4.0f)
            || !terrainMaterialRange(m.response[3], 0.0f, 1.0f)
            || !terrainMaterialRange(m.ramp[0], 0.05f, 0.95f)
            || !terrainMaterialRange(m.ramp[1], 0.0f, 1.0f)
            || !terrainMaterialRange(m.ramp[2], 0.0f, 8.0f)
            || !terrainMaterialRange(m.ramp[3], -1.0f, 1.0f)
            || !terrainMaterialRange(m.fineSurface[0], 0.0f, 1.0f)
            || !terrainMaterialRange(m.fineSurface[1], 0.04f, 1.0f)
            || !terrainMaterialRange(m.fineSurface[2], 0.0f, 1.0f)
            || !terrainMaterialRange(m.fineSurface[3], 0.0f, 1.0f)) return false;
        for (float weight : m.pattern)
            if (!terrainMaterialRange(weight, 0.0f, 4.0f)) return false;
        return true;
    }

    inline bool validTerrainSurface(const TerrainSurface& s) noexcept
    {
        return validTerrainMaterial(s.base) && validTerrainMaterial(s.accent)
            && terrainMaterialRange(s.blend[0], 0.0f, 1.0f)
            && terrainMaterialRange(s.blend[1], 0.0f, 1.0f)
            && terrainMaterialRange(s.blend[2], 0.0f, 4.0f)
            && terrainMaterialRange(s.blend[3], 0.0f, 4.0f)
            && terrainMaterialRange(s.detail[0], 0.01f, 100000.0f)
            && terrainMaterialRange(s.detail[1], 0.01f, 100000.0f)
            && terrainMaterialRange(s.detail[2], -6.283186f, 6.283186f)
            && terrainMaterialRange(s.detail[3], 0.0f, 10000.0f)
            && terrainMaterialRange(s.normal[0], 0.0f, 7.0f)
            && s.normal[0] == static_cast<float>(static_cast<int>(s.normal[0]))
            && terrainMaterialRange(s.normal[1], 0.01f, 10000.0f)
            && terrainMaterialRange(s.normal[2], 0.0f, 2.0f)
            && terrainMaterialRange(s.normal[3], 0.0f, 1.0f);
    }

    inline bool validTerrainMaterialTable(const TerrainMaterialTable& table) noexcept
    {
        for (const auto& b : table)
        {
            for (float weight : b.stainRouting)
                if (!terrainMaterialRange(weight, 0.f, 1.f)) return false;
            if (!validTerrainSurface(b.ground) || !validTerrainSurface(b.wall)
                || !terrainMaterialRange(b.slope[0], 0.0f, 1.0f)
                || !terrainMaterialRange(b.slope[1], 0.0f, 1.0f)
                || b.slope[1] <= b.slope[0]
                || !terrainMaterialRange(b.slope[2], 0.0f, 1.0f)
                || !terrainMaterialRange(b.slope[3], 0.0f, 4.0f)
                || !terrainMaterialRange(b.spectrum[0], 0.05f, 64.0f)
                || !terrainMaterialRange(b.spectrum[1], 0.01f, 1000.0f)
                || !terrainMaterialRange(b.spectrum[2], 1.0f, 3.0f)
                || !terrainMaterialRange(b.spectrum[3], 0.0f, 0.5f)) return false;
        }
        return true;
    }

    inline bool validTerrainAppearance(const TerrainAppearance& appearance) noexcept
    {
        return validTerrainMaterialTable(appearance.biomes)
            && terrainMaterialRange(appearance.macroPeriod, 1.0f, 100000.0f)
            && terrainMaterialRange(appearance.patchPeriod, 0.1f, 100000.0f)
            && terrainMaterialRange(appearance.vegetationStainSize, 0.005f, 1000.0f)
            && terrainMaterialRange(appearance.otherStainSize, 0.005f, 1000.0f)
            && terrainMaterialRange(appearance.sparkleGrainSize, 0.002f, 1.0f)
            && appearance.debugView >= 0 && appearance.debugView <= 6;
    }
}

#endif
