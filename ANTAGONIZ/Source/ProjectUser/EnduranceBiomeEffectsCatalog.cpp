#include "EnduranceBiomeEffectsCatalog.hpp"
#include <cstring>
#include <initializer_list>

namespace EnduranceEnvironment
{
    namespace
    {
        using Id = EffectId;
        using Shape = ParticleShape;
        using V3 = glm::vec3;
        using V4 = glm::vec4;

        ParticlePreset particles(Shape shape, std::uint32_t count, V3 extent,
            V3 offset, V3 direction, float minSpeed, float maxSpeed,
            float minLife, float maxLife, float minSize, float maxSize,
            V4 color, V3 force = V3(0.0f))
        {
            ParticlePreset p;
            p.shape = shape; p.count = count;
            auto& g = p.group;
            g.flags = Ge::BoxEmit | Ge::ScaleRNG | Ge::InstantParticuleInitialize;
            g.emitPosition = offset;
            g.emitScaleMin = -extent; g.emitScaleMax = extent;
            g.emitDirection = glm::normalize(direction);
            g.spreadAngle = 18.0f;
            g.velocityMin = minSpeed; g.velocityMax = maxSpeed;
            g.lifeTimeMin = minLife; g.lifeTimeMax = maxLife;
            g.scaleStart = minSize; g.scaleEnd = maxSize;
            g.gravity = V3(0.0f); g.externalForce = force;
            g.drag = 0.12f;
            // Initialize the entire uploaded array, including inactive stops.
            // Runtime settings are byte-compared to avoid redundant uploads.
            for (auto& stop : g.colorGradient) stop = V4(0.0f);
            g.nbColor = 3;
            g.colorGradient[0] = V4(V3(color), 0.0f);
            g.colorGradient[1] = color;
            g.colorGradient[2] = V4(V3(color) * 0.82f, 0.0f);
            g.rotationMin = -35.0f; g.rotationMax = 35.0f;
            g.rotationScaleMin = 0.12f; g.rotationScaleMax = 0.35f;
            g.rotateSprite = 0.4f;
            return p;
        }

        EffectMask mask(std::initializer_list<Id> ids)
        {
            EffectMask result = 0;
            for (const auto id : ids) result |= effectBit(id);
            return result;
        }

        std::array<EffectDefinition, EffectCount> makeDefinitions()
        {
            std::array<EffectDefinition, EffectCount> result{};
            auto define = [&](Id id, const char* key, const char* name, bool event = false) -> EffectDefinition&
            {
                auto& d = result[static_cast<std::size_t>(id)];
                d.id = id; d.key = key; d.name = name; d.activeEvent = event;
                d.fadeIn = event ? 2.5f : 1.2f; d.fadeOut = event ? 2.0f : 1.0f;
                return d;
            };
            auto& heat = define(Id::HeatHaze, "heat_haze", "Mirage de chaleur");
            heat.screen.heat = 0.30f; heat.screen.scale = 0.75f; heat.screen.speed = 0.55f;
            heat.fadeIn = heat.fadeOut = 2.0f;

            auto& sand = define(Id::SandGust, "sand_gust", "Bourrasques de sable");
            sand.particles = particles(Shape::Dust, 1536, {23, 1.4f, 23}, {0, -0.8f, 0},
                {1, 0.04f, 0.3f}, 2.5f, 5.0f, 4, 7, 0.12f, 0.38f, {0.65f, 0.48f, 0.27f, 0.23f});
            sand.particles.turbulence = 0.6f; sand.particles.windScale = 1.6f;

            auto& ash = define(Id::VolcanicAsh, "volcanic_ash", "Cendres volcaniques");
            ash.particles = particles(Shape::Flake, 1152, {22, 9, 22}, {0, 5, 0},
                {0.18f, -1, 0.1f}, 0.5f, 1.1f, 10, 16, 0.025f, 0.085f, {0.32f, 0.29f, 0.27f, 0.65f});
            ash.particles.turbulence = 0.45f; ash.particles.turbulenceScale = 0.18f;
            ash.particles.group.flags |= Ge::RotateSprite;

            auto& pollen = define(Id::WindPollen, "wind_pollen", "Pollen au vent");
            pollen.particles = particles(Shape::Seed, 384, {18, 5, 18}, {0, 1, 0},
                {1, 0.1f, 0.4f}, 0.3f, 0.8f, 8, 14, 0.022f, 0.055f, {0.76f, 0.70f, 0.35f, 0.62f});
            pollen.particles.turbulence = 0.25f; pollen.particles.windScale = 0.8f;

            auto& fireflies = define(Id::Fireflies, "fireflies", "Lucioles multicolores");
            fireflies.particles = particles(Shape::Glow, 72, {16, 3.5f, 16}, {0, 0.6f, 0},
                {0.3f, 0.5f, 0.1f}, 0.05f, 0.2f, 12, 20, 0.035f, 0.09f, {0.65f, 0.95f, 0.24f, 0.80f});
            fireflies.particles.group.nbColor = 4;
            fireflies.particles.group.colorGradient[0] = V4(0.14f, 0.85f, 0.72f, 0.0f);
            fireflies.particles.group.colorGradient[1] = V4(0.55f, 0.95f, 0.16f, 0.75f);
            fireflies.particles.group.colorGradient[2] = V4(0.90f, 0.48f, 0.12f, 0.70f);
            fireflies.particles.group.colorGradient[3] = V4(0.3f, 0.48f, 0.95f, 0.0f);
            fireflies.particles.emission = 3.5f; fireflies.particles.pulse = 1.8f;
            fireflies.particles.turbulence = 0.45f; fireflies.particles.windScale = 0.08f;

            auto& snow = define(Id::LightSnow, "light_snow", "Neige légère");
            snow.particles = particles(Shape::Flake, 1536, {23, 11, 23}, {0, 5, 0},
                {0.08f, -1, 0.04f}, 0.6f, 1.4f, 10, 18, 0.035f, 0.11f, {0.89f, 0.94f, 1.0f, 0.72f});
            snow.particles.turbulence = 0.55f; snow.particles.group.flags |= Ge::RotateSprite;

            auto& frost = define(Id::FrostRim, "frost_rim", "Givre au bord de l'écran");
            frost.screen.frost = 0.26f; frost.screen.scale = 1.25f; frost.screen.speed = 0.12f;
            frost.fadeIn = 3.5f; frost.fadeOut = 2.5f;

            auto& crystals = define(Id::IceCrystals, "ice_crystals", "Cristaux de glace");
            crystals.particles = particles(Shape::Crystal, 256, {18, 7, 18}, {0, 2, 0},
                {0.35f, -0.8f, 0.18f}, 0.12f, 0.48f, 9, 15, 0.025f, 0.055f, {0.64f, 0.86f, 1.0f, 0.66f});
            crystals.particles.emission = 0.8f; crystals.particles.pulse = 2.3f;
            crystals.particles.group.flags |= Ge::RotateSprite;

            auto& drift = define(Id::GroundSnowDrift, "ground_snow_drift", "Poudrerie au ras du sol");
            drift.particles = particles(Shape::Mist, 80, {28, 0.65f, 28}, {0, -1.15f, 0},
                {1, 0.02f, 0.2f}, 2.0f, 4.5f, 4, 7, 0.4f, 1.15f, {0.80f, 0.89f, 0.95f, 0.065f});
            drift.particles.aspect = 2.8f; drift.particles.turbulence = 0.4f; drift.particles.windScale = 1.8f;

            auto& leaves = define(Id::WindLeaves, "wind_leaves", "Feuilles emportées");
            leaves.particles = particles(Shape::Leaf, 192, {22, 7, 22}, {0, 3, 0},
                {0.5f, -0.35f, 0.4f}, 0.45f, 1.2f, 7, 12, 0.07f, 0.18f, {0.35f, 0.40f, 0.16f, 0.80f});
            leaves.particles.aspect = 1.65f; leaves.particles.group.flags |= Ge::RotateSprite;
            leaves.particles.group.rotateSprite = 1.4f;
            leaves.particles.group.gravity = V3(0, -0.07f, 0); leaves.particles.turbulence = 0.75f;

            auto& petals = define(Id::Petals, "petals", "Pétales flottants");
            petals.particles = particles(Shape::Petal, 208, {19, 5, 19}, {0, 2, 0},
                {0.6f, -0.2f, 0.25f}, 0.2f, 0.6f, 8, 14, 0.045f, 0.12f, {0.82f, 0.43f, 0.47f, 0.72f});
            petals.particles.group.flags |= Ge::RotateSprite; petals.particles.turbulence = 0.55f;
            petals.particles.group.rotateSprite = 0.85f;
            petals.particles.aspect = 1.25f;

            auto& seeds = define(Id::DrySeeds, "dry_seeds", "Graines et aigrettes sèches");
            seeds.particles = particles(Shape::Seed, 384, {22, 3, 22}, {0, 0.5f, 0},
                {1, 0.12f, 0.18f}, 0.7f, 1.5f, 7, 12, 0.045f, 0.10f, {0.62f, 0.57f, 0.38f, 0.60f});
            seeds.particles.aspect = 1.8f; seeds.particles.turbulence = 0.35f; seeds.particles.windScale = 1.25f;

            auto& motes = define(Id::DustMotes, "dust_motes", "Poussières en suspension");
            motes.particles = particles(Shape::Dust, 384, {17, 6, 17}, {0, 1, 0},
                {0.5f, 0.2f, 0.3f}, 0.025f, 0.12f, 12, 22, 0.012f, 0.035f, {0.66f, 0.61f, 0.48f, 0.46f});
            motes.particles.windScale = 0.2f; motes.particles.turbulence = 0.1f;

            auto& mist = define(Id::GroundMist, "ground_mist", "Nappes de brume basse");
            mist.particles = particles(Shape::Mist, 40, {28, 0.55f, 28}, {0, -1.2f, 0},
                {1, 0.01f, 0.1f}, 0.08f, 0.22f, 14, 24, 1.0f, 2.4f, {0.52f, 0.61f, 0.64f, 0.055f});
            mist.particles.aspect = 3.4f; mist.particles.windScale = 0.2f;

            auto& swamp = define(Id::SwampVapor, "swamp_vapor", "Vapeurs de marécage");
            swamp.particles = particles(Shape::Mist, 48, {23, 0.7f, 23}, {0, -1, 0},
                {0.1f, 1, 0.1f}, 0.10f, 0.3f, 9, 16, 0.5f, 1.3f, {0.34f, 0.46f, 0.35f, 0.075f});
            swamp.particles.turbulence = 0.18f; swamp.particles.turbulenceScale = 0.08f;
            swamp.particles.windScale = 0.12f;

            auto& spray = define(Id::SeaSpray, "sea_spray", "Embruns marins");
            spray.particles = particles(Shape::Droplet, 896, {23, 2.5f, 23}, {0, -0.7f, 0},
                {1, 0.3f, 0.1f}, 1.2f, 3.2f, 3, 6, 0.022f, 0.065f, {0.70f, 0.87f, 0.89f, 0.45f});
            spray.particles.group.gravity = V3(0, -0.45f, 0); spray.particles.windScale = 1.3f;
            spray.particles.aspect = 1.4f; spray.particles.turbulence = 0.3f;

            auto& salt = define(Id::SaltMist, "salt_mist", "Voiles salins");
            salt.particles = particles(Shape::Mist, 40, {28, 2, 28}, {0, -0.7f, 0},
                {1, 0.02f, 0.3f}, 0.35f, 0.8f, 10, 18, 0.75f, 1.7f, {0.55f, 0.73f, 0.77f, 0.045f});
            salt.particles.aspect = 2.4f; salt.particles.windScale = 0.7f;

            auto& embers = define(Id::VolcanicEmbers, "volcanic_embers", "Braises volcaniques");
            embers.particles = particles(Shape::Ember, 192, {21, 2, 21}, {0, -0.7f, 0},
                {0.15f, 1, 0.08f}, 0.35f, 1.4f, 5, 10, 0.025f, 0.07f, {1.0f, 0.20f, 0.025f, 0.85f});
            embers.particles.emission = 4.0f; embers.particles.pulse = 3.2f;
            embers.particles.turbulence = 0.5f; embers.particles.windScale = 0.6f;

            auto& fumes = define(Id::Fumaroles, "fumaroles", "Fumerolles");
            fumes.particles = particles(Shape::Mist, 40, {22, 0.25f, 22}, {0, -1.5f, 0},
                {0.02f, 1, 0.02f}, 0.4f, 0.8f, 7, 14, 0.4f, 1.2f, {0.34f, 0.31f, 0.26f, 0.10f});
            fumes.particles.aspect = 1.4f; fumes.particles.turbulence = 0.25f;
            fumes.particles.windScale = 0.25f;

            auto& spores = define(Id::Spores, "spores", "Spores flottantes");
            spores.particles = particles(Shape::Dust, 384, {18, 5, 18}, {0, 1.0f, 0},
                {0.15f, 1, 0.1f}, 0.035f, 0.15f, 13, 22, 0.025f, 0.065f, {0.52f, 0.58f, 0.34f, 0.58f});
            spores.particles.windScale = 0.15f; spores.particles.turbulence = 0.16f;

            auto& glowing = define(Id::GlowingSpores, "glowing_spores", "Spores bioluminescentes");
            glowing.particles = particles(Shape::Glow, 128, {16, 4, 16}, {0, 0.6f, 0},
                {0.1f, 1, 0.2f}, 0.06f, 0.18f, 10, 18, 0.03f, 0.075f, {0.18f, 0.66f, 0.83f, 0.70f});
            glowing.particles.emission = 2.3f; glowing.particles.pulse = 0.7f;
            glowing.particles.windScale = 0.12f; glowing.particles.turbulence = 0.15f;

            auto& drips = define(Id::CanopyDrips, "canopy_drips", "Gouttes de la canopée");
            drips.particles = particles(Shape::Droplet, 384, {20, 3, 20}, {0, 5, 0},
                {0, -1, 0}, 1.0f, 2.4f, 2.5f, 5, 0.018f, 0.04f, {0.58f, 0.73f, 0.68f, 0.50f});
            drips.particles.aspect = 3.5f; drips.particles.windScale = 0.12f;
            drips.particles.group.gravity = V3(0, -2.5f, 0); drips.particles.group.flags |= Ge::VelOrientation;

            auto& insects = define(Id::Insects, "insects", "Insectes en vol");
            insects.particles = particles(Shape::Insect, 64, {14, 2.7f, 14}, {0, 0.2f, 0},
                {0.4f, 0.2f, 0.3f}, 0.1f, 0.5f, 8, 14, 0.02f, 0.045f, {0.14f, 0.17f, 0.10f, 0.85f});
            insects.particles.turbulence = 0.85f; insects.particles.turbulenceScale = 0.8f;
            insects.particles.windScale = 0.08f; insects.particles.pulse = 8.0f;

            auto& streaks = define(Id::WindStreaks, "wind_streaks", "Filets de vent");
            streaks.particles = particles(Shape::Streak, 96, {25, 4, 25}, {0, 0.2f, 0},
                {1, 0.03f, 0.2f}, 4.0f, 7.5f, 2.5f, 4.5f, 0.025f, 0.06f, {0.64f, 0.70f, 0.68f, 0.13f});
            streaks.particles.aspect = 12.0f; streaks.particles.windScale = 1.4f;
            streaks.particles.group.flags |= Ge::VelOrientation;

            auto& breath = define(Id::ColdBreath, "cold_breath", "Souffle dans le froid");
            breath.particles = particles(Shape::Mist, 12, {0.12f, 0.06f, 0.12f}, {0, -0.28f, -0.65f},
                {0, 0.2f, -1}, 0.18f, 0.4f, 1.0f, 2.0f, 0.06f, 0.16f, {0.76f, 0.84f, 0.88f, 0.07f});
            breath.particles.aspect = 1.4f; breath.particles.pulse = 0.45f;
            breath.particles.windScale = 0.05f; breath.particles.turbulence = 0.07f;

            auto& wet = define(Id::WetLens, "wet_lens", "Gouttelettes sur l'écran");
            wet.screen.wet = 0.16f; wet.screen.scale = 1.1f; wet.screen.speed = 0.25f;
            wet.fadeIn = 3.0f; wet.fadeOut = 2.0f;

            auto& condensation = define(Id::Condensation, "condensation", "Buée périphérique");
            condensation.screen.condensation = 0.14f; condensation.screen.scale = 0.8f;
            condensation.screen.speed = 0.12f; condensation.fadeIn = condensation.fadeOut = 3.0f;

            auto& veil = define(Id::SandVeil, "sand_veil", "Voile de poussière chaude");
            veil.screen.dust = 0.10f; veil.screen.tint = V3(0.62f, 0.38f, 0.19f);
            veil.screen.scale = 0.65f; veil.screen.speed = 0.45f;

            auto& rain = define(Id::Rain, "rain", "Pluie", true);
            rain.particles = particles(Shape::Streak, 3072, {28, 14, 28}, {0, 8, 0},
                {0.08f, -1, 0.03f}, 7, 11, 2.5f, 4.5f, 0.018f, 0.035f, {0.58f, 0.69f, 0.76f, 0.42f});
            rain.particles.aspect = 14.0f; rain.particles.group.flags |= Ge::VelOrientation;
            rain.particles.group.gravity = V3(0, -0.6f, 0); rain.screen.wet = 0.25f;

            auto& heavy = define(Id::HeavyRain, "heavy_rain", "Pluie battante", true);
            heavy.particles = particles(Shape::Streak, 6144, {31, 17, 31}, {0, 10, 0},
                {0.3f, -1, 0.12f}, 12, 17, 2, 3.5f, 0.022f, 0.045f, {0.52f, 0.64f, 0.72f, 0.53f});
            heavy.particles.aspect = 18.0f; heavy.particles.windScale = 1.8f;
            heavy.particles.group.flags |= Ge::VelOrientation; heavy.screen.wet = 0.56f;
            heavy.screen.condensation = 0.09f; heavy.screen.speed = 1.6f;

            auto& storm = define(Id::Sandstorm, "sandstorm", "Tempête de sable", true);
            storm.particles = particles(Shape::Dust, 6144, {32, 13, 32}, {0, 2, 0},
                {1, 0.08f, 0.25f}, 8, 15, 3.5f, 6, 0.1f, 0.32f, {0.53f, 0.34f, 0.16f, 0.43f});
            storm.particles.aspect = 2.5f; storm.particles.windScale = 3.5f;
            storm.particles.turbulence = 1.8f; storm.screen.dust = 0.65f;
            storm.screen.heat = 0.10f; storm.screen.tint = V3(0.57f, 0.36f, 0.16f);
            storm.screen.speed = 2.0f; storm.fadeIn = 5.0f;

            auto& blizzard = define(Id::Blizzard, "blizzard", "Tempête de neige", true);
            blizzard.particles = particles(Shape::Flake, 6144, {30, 14, 30}, {0, 4, 0},
                {1, -0.4f, 0.2f}, 5, 11, 4, 7, 0.045f, 0.15f, {0.85f, 0.92f, 1.0f, 0.74f});
            blizzard.particles.group.flags |= Ge::RotateSprite;
            blizzard.particles.group.rotateSprite = 1.0f;
            blizzard.particles.turbulence = 2.0f; blizzard.particles.windScale = 3.0f;
            blizzard.screen.frost = 0.55f; blizzard.screen.dust = 0.30f;
            blizzard.screen.tint = V3(0.70f, 0.81f, 0.91f); blizzard.screen.speed = 1.7f;
            blizzard.fadeIn = 4.0f;

            auto& ashStorm = define(Id::AshStorm, "ash_storm", "Tempête de cendres", true);
            ashStorm.particles = particles(Shape::Flake, 4096, {30, 12, 30}, {0, 5, 0},
                {0.9f, -0.5f, 0.2f}, 3, 7, 6, 10, 0.04f, 0.13f, {0.25f, 0.22f, 0.20f, 0.72f});
            ashStorm.particles.group.flags |= Ge::RotateSprite;
            ashStorm.particles.group.rotateSprite = 0.9f;
            ashStorm.particles.turbulence = 1.3f; ashStorm.particles.windScale = 2.4f;
            ashStorm.screen.dust = 0.50f; ashStorm.screen.tint = V3(0.24f, 0.20f, 0.17f);
            ashStorm.screen.speed = 1.4f;

            auto& thunder = define(Id::Thunderstorm, "thunderstorm", "Orage lumineux", true);
            thunder.particles = particles(Shape::Streak, 4608, {30, 16, 30}, {0, 9, 0},
                {0.22f, -1, 0.18f}, 10, 15, 2.2f, 4, 0.02f, 0.04f, {0.48f, 0.60f, 0.70f, 0.47f});
            thunder.particles.aspect = 16.0f; thunder.particles.windScale = 2.0f;
            thunder.particles.group.flags |= Ge::VelOrientation;
            thunder.screen.wet = 0.40f; thunder.screen.flash = 0.65f;
            thunder.screen.speed = 1.1f;

            auto& hail = define(Id::Hail, "hail", "Grêle", true);
            hail.particles = particles(Shape::Crystal, 2048, {27, 15, 27}, {0, 8, 0},
                {0.08f, -1, 0.03f}, 8, 13, 2.5f, 4.5f, 0.035f, 0.085f, {0.71f, 0.85f, 0.92f, 0.84f});
            hail.particles.group.gravity = V3(0, -1.5f, 0); hail.particles.aspect = 1.2f;
            hail.particles.group.flags |= Ge::RotateSprite; hail.screen.wet = 0.18f;
            hail.particles.group.rotateSprite = 1.2f;

            auto& sleet = define(Id::Sleet, "sleet", "Grésil", true);
            sleet.particles = particles(Shape::Streak, 3584, {28, 13, 28}, {0, 7, 0},
                {0.28f, -1, 0.12f}, 4, 8, 3, 5.5f, 0.025f, 0.07f, {0.67f, 0.80f, 0.87f, 0.64f});
            sleet.particles.aspect = 4.5f; sleet.particles.windScale = 1.6f;
            sleet.particles.group.flags |= Ge::VelOrientation;
            sleet.screen.wet = 0.26f; sleet.screen.frost = 0.13f;

            auto& meteors = define(Id::MeteorShower, "meteor_shower", "Pluie de fragments incandescents", true);
            meteors.particles = particles(Shape::Meteor, 32, {220, 80, 220}, {0, 180, 0},
                {0.65f, -1, 0.25f}, 30, 55, 3, 7, 0.15f, 0.45f, {1.0f, 0.45f, 0.12f, 0.90f});
            meteors.particles.aspect = 22.0f; meteors.particles.emission = 6.0f;
            meteors.particles.group.flags |= Ge::VelOrientation; meteors.particles.windScale = 0.0f;
            meteors.particles.group.drag = 0.0f;

            auto& lunar = define(Id::LunarDustFall, "lunar_dust_fall", "Retombées de poussière lunaire", true);
            lunar.particles = particles(Shape::Crystal, 1280, {31, 15, 31}, {0, 7, 0},
                {0.1f, -1, 0.1f}, 0.6f, 1.8f, 12, 20, 0.035f, 0.12f, {0.46f, 0.49f, 0.58f, 0.65f});
            lunar.particles.group.flags |= Ge::RotateSprite;
            lunar.particles.turbulence = 0.32f; lunar.particles.emission = 0.12f;
            lunar.screen.dust = 0.12f; lunar.screen.tint = V3(0.40f, 0.43f, 0.52f);
            lunar.screen.speed = 0.25f; lunar.fadeIn = 4.0f;

            auto& pollenBurst = define(Id::PollenBurst, "pollen_burst", "Envol massif de pollen", true);
            pollenBurst.particles = particles(Shape::Seed, 2048, {26, 6, 26}, {0, 1, 0},
                {1, 0.18f, 0.25f}, 1.2f, 2.8f, 7, 12, 0.025f, 0.085f, {0.75f, 0.67f, 0.26f, 0.65f});
            pollenBurst.particles.turbulence = 0.7f; pollenBurst.particles.windScale = 1.4f;
            pollenBurst.screen.dust = 0.07f; pollenBurst.screen.tint = V3(0.65f, 0.59f, 0.24f);

            auto& bloom = define(Id::SporeBloom, "spore_bloom", "Floraison de spores lumineuses", true);
            bloom.particles = particles(Shape::Glow, 960, {24, 7, 24}, {0, 1, 0},
                {0.1f, 1, 0.1f}, 0.15f, 0.45f, 10, 18, 0.035f, 0.11f, {0.27f, 0.72f, 0.59f, 0.68f});
            bloom.particles.group.nbColor = 4;
            bloom.particles.group.colorGradient[0] = V4(0.16f, 0.52f, 0.65f, 0.0f);
            bloom.particles.group.colorGradient[1] = V4(0.24f, 0.68f, 0.50f, 0.65f);
            bloom.particles.group.colorGradient[2] = V4(0.52f, 0.28f, 0.72f, 0.55f);
            bloom.particles.group.colorGradient[3] = V4(0.16f, 0.52f, 0.65f, 0.0f);
            bloom.particles.emission = 2.8f; bloom.particles.pulse = 0.9f;
            bloom.particles.turbulence = 0.35f; bloom.particles.windScale = 0.2f;
            return result;
        }
    }

    const std::array<EffectDefinition, EffectCount>& effectDefinitions()
    {
        static const auto definitions = makeDefinitions();
        return definitions;
    }

    const std::array<BiomeEffects, ProceduralTerrain::BiomeCount>& biomeEffectMatrix()
    {
        // These are eligibility masks, never interpolation weights. A controller
        // uses exactly one row: the dominant biome's row. Active masks only
        // authorize an explicit event request; they do not launch events.
        static const std::array<BiomeEffects, ProceduralTerrain::BiomeCount> matrix{{
            {mask({Id::LightSnow,Id::FrostRim,Id::IceCrystals,Id::ColdBreath}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::GroundSnowDrift,Id::IceCrystals,Id::WindStreaks}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::DrySeeds,Id::LightSnow,Id::ColdBreath}), mask({Id::Blizzard,Id::Sleet,Id::Hail,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::LightSnow,Id::WindLeaves,Id::DustMotes}), mask({Id::Rain,Id::Blizzard,Id::Sleet,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::GroundMist,Id::Spores,Id::Condensation}), mask({Id::Rain,Id::Sleet,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::WindStreaks,Id::IceCrystals,Id::ColdBreath}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::GroundSnowDrift,Id::DrySeeds,Id::WindStreaks}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::WindPollen,Id::WindLeaves,Id::LightSnow}), mask({Id::Rain,Id::Sleet,Id::Blizzard,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::GroundMist,Id::CanopyDrips,Id::Condensation}), mask({Id::Rain,Id::HeavyRain,Id::Hail,Id::Sleet,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::SwampVapor,Id::GlowingSpores,Id::FrostRim}), mask({Id::Rain,Id::Sleet,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::DrySeeds,Id::DustMotes,Id::WindStreaks}), mask({Id::Rain,Id::Sandstorm,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::WindPollen,Id::Petals,Id::Insects}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::Hail,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::WindLeaves,Id::Fireflies,Id::DustMotes}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::CanopyDrips,Id::WetLens,Id::Condensation}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::SwampVapor,Id::Insects,Id::Fireflies}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::HeatHaze,Id::SandGust,Id::DustMotes}), mask({Id::Sandstorm,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::HeatHaze,Id::DrySeeds,Id::DustMotes}), mask({Id::Rain,Id::Sandstorm,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::WindPollen,Id::DrySeeds,Id::Insects}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::Spores,Id::WindLeaves,Id::HeatHaze}), mask({Id::Rain,Id::Thunderstorm,Id::PollenBurst,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::Spores,Id::GlowingSpores,Id::WetLens,Id::CanopyDrips}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::VolcanicAsh,Id::VolcanicEmbers,Id::Fumaroles,Id::HeatHaze}), mask({Id::AshStorm,Id::Sandstorm,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::SandGust,Id::SandVeil,Id::HeatHaze}), mask({Id::Sandstorm,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::SandGust,Id::DustMotes,Id::WindStreaks}), mask({Id::Sandstorm,Id::Hail,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::VolcanicAsh,Id::VolcanicEmbers,Id::Fumaroles,Id::HeatHaze}), mask({Id::AshStorm,Id::Rain,Id::Thunderstorm,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::SeaSpray,Id::SwampVapor,Id::Fireflies,Id::CanopyDrips}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::SeaSpray,Id::SaltMist,Id::WindStreaks}), mask({Id::Rain,Id::HeavyRain,Id::Thunderstorm,Id::Hail,Id::MeteorShower,Id::LunarDustFall})}
        }};
        return matrix;
    }

    const EffectDefinition* findEffect(const char* key)
    {
        if (!key) return nullptr;
        for (const auto& definition : effectDefinitions())
            if (std::strcmp(definition.key, key) == 0) return &definition;
        return nullptr;
    }
}
