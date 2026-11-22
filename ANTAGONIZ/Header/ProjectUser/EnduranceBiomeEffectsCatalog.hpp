#pragma once

#include "ParticleSettings.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

// Game-authored recipes. Definitions and biome assignments allocate no GL
// resources; the renderers instantiate only effects requested by the controller.
namespace EnduranceEnvironment
{
    enum class EffectId : std::uint8_t
    {
        HeatHaze, SandGust, VolcanicAsh, WindPollen, Fireflies, LightSnow,
        FrostRim, IceCrystals, GroundSnowDrift, WindLeaves, Petals, DrySeeds,
        DustMotes, GroundMist, SwampVapor, SeaSpray, SaltMist, VolcanicEmbers,
        Fumaroles, Spores, GlowingSpores, CanopyDrips, Insects, WindStreaks,
        ColdBreath, WetLens, Condensation, SandVeil,
        Rain, HeavyRain, Sandstorm, Blizzard, AshStorm, Thunderstorm,
        Hail, Sleet, MeteorShower, LunarDustFall, PollenBurst, SporeBloom,
        Count
    };
    inline constexpr std::size_t EffectCount = static_cast<std::size_t>(EffectId::Count);
    static_assert(EffectCount == 40, "Keep stable IDs for all 40 biome effects");
    using EffectMask = std::uint64_t;
    constexpr EffectMask effectBit(EffectId id) noexcept
    {
        return static_cast<std::size_t>(id) < EffectCount
            ? (EffectMask{1} << static_cast<unsigned>(id)) : EffectMask{0};
    }

    // Shapes select bounded shader variants, not forty independent simulations.
    enum class ParticleShape : std::uint8_t
    {
        Dust, Flake, Streak, Leaf, Petal, Seed, Glow, Mist, Ember, Crystal,
        Droplet, Insect, Meteor, Count
    };

    struct ParticlePreset
    {
        std::uint32_t count = 0;
        ParticleShape shape = ParticleShape::Dust;
        // emitPosition is an offset from the camera; spawn extents and forces
        // remain in world axes. The renderer adds the live camera position.
        Ge::ParticleBaseGroup group;
        float aspect = 1.0f;
        float emission = 0.0f;
        float turbulence = 0.0f;
        float turbulenceScale = 0.12f;
        float pulse = 0.0f;
        float windScale = 1.0f;
    };

    enum ScreenFeature : std::uint32_t
    {
        Heat = 1u << 0, Frost = 1u << 1, Wet = 1u << 2,
        Condensation = 1u << 3, Dust = 1u << 4, Flash = 1u << 5
    };
    struct ScreenPreset
    {
        float heat = 0.0f, frost = 0.0f, wet = 0.0f;
        float condensation = 0.0f, dust = 0.0f, flash = 0.0f;
        glm::vec3 tint{1.0f};
        float scale = 1.0f, speed = 1.0f;
        std::uint32_t features() const noexcept
        {
            return (heat > 0 ? Heat : 0u) | (frost > 0 ? Frost : 0u)
                | (wet > 0 ? Wet : 0u) | (condensation > 0 ? Condensation : 0u)
                | (dust > 0 ? Dust : 0u) | (flash > 0 ? Flash : 0u);
        }
    };

    struct EffectDefinition
    {
        EffectId id = EffectId::HeatHaze;
        const char* key = "";
        const char* name = "";
        bool activeEvent = false;
        ParticlePreset particles;
        ScreenPreset screen;
        float fadeIn = 1.0f, fadeOut = 1.0f;
    };
    struct BiomeEffects
    {
        EffectMask passive = 0;
        EffectMask active = 0;
    };

    const std::array<EffectDefinition, EffectCount>& effectDefinitions();
    const std::array<BiomeEffects, ProceduralTerrain::BiomeCount>& biomeEffectMatrix();
    const EffectDefinition* findEffect(const char* key);
}
