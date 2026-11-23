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
            p.shape = shape; p.count = count * ParticlePopulationScale;
            // The simulation stratifies this enlarged XZ volume near/mid/far.
            // Keeping authored vertical layers avoids diluting fog over 1000x volume.
            extent *= V3(ParticleHorizontalReachScale, 1.0f, ParticleHorizontalReachScale);
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
            heat.screen.heat = .65f;
            heat.screen.scale = 1.05f;
            heat.screen.speed = .85f;
            heat.fadeIn = heat.fadeOut = 2.0f;

            auto& sand = define(Id::SandGust, "sand_gust", "Bourrasques de sable");
            sand.particles = particles(Shape::Dust, 1536, {23, 1.4f, 23}, {0, -0.8f, 0},
                {1, 0.1f, 0.3f}, .8f, 2.6f, 9, 16, .035f, .14f, {.65f,.48f,.27f,.32f});

            sand.particles.turbulence = .65f;
            sand.particles.turbulenceScale = .05f;
            sand.particles.windScale = .9f;
            sand.particles.group.drag = .8f;
            sand.particles.group.spreadAngle = 35.f;

            auto& ash = define(Id::VolcanicAsh, "volcanic_ash", "Cendres volcaniques");
            ash.particles = particles(Shape::Ash, 1152, {22,18,22}, {0,7,0},
                {.04f,-1,.03f}, .16f, .52f, 20, 34, 0.025f, 0.085f, {0.32f, 0.29f, 0.27f, 0.65f});
            ash.particles.group.flags |= Ge::RotateSprite;

            ash.particles.turbulence = .32f;
            ash.particles.turbulenceScale = .07f;
            ash.particles.windScale = .32f;
            ash.particles.group.drag = .9f;
            ash.particles.group.spreadAngle = 28.f;
            ash.particles.group.rotateSprite = .75f;

            auto& pollen = define(Id::WindPollen, "wind_pollen", "Pollen au vent");
            pollen.particles = particles(Shape::Pollen, 384, {18, 5, 18}, {0, 1, 0},
                {.15f,.5f,.08f}, .06f, .18f, 18, 30, 0.022f, 0.055f, {0.76f, 0.70f, 0.35f, 0.62f});

            pollen.particles.turbulence = .28f;
            pollen.particles.turbulenceScale = .11f;
            pollen.particles.windScale = .4f;
            pollen.particles.group.drag = 1.1f;
            pollen.particles.group.spreadAngle = 70.f;

            auto& fireflies = define(Id::Fireflies, "fireflies", "Lucioles multicolores");
            fireflies.particles = particles(Shape::Glow, 72, {16, 3.5f, 16}, {0, 0.6f, 0},
                {.3f,.25f,.1f}, .025f, .12f, 18, 32, 0.035f, 0.09f, {0.65f, 0.95f, 0.24f, 0.80f});
            fireflies.particles.group.nbColor = 4;
            fireflies.particles.group.colorGradient[0] = V4(0.14f, 0.85f, 0.72f, 0.0f);
            fireflies.particles.group.colorGradient[1] = V4(0.55f, 0.95f, 0.16f, 0.75f);
            fireflies.particles.group.colorGradient[2] = V4(0.90f, 0.48f, 0.12f, 0.70f);
            fireflies.particles.group.colorGradient[3] = V4(0.3f, 0.48f, 0.95f, 0.0f);
            fireflies.particles.emission = 3.5f;

            fireflies.particles.pulse = .85f;
            fireflies.particles.turbulence = .9f;
            fireflies.particles.turbulenceScale = .17f;
            fireflies.particles.windScale = .025f;
            fireflies.particles.group.drag = 1.5f;
            fireflies.particles.group.spreadAngle = 150.f;

            // Terminal fall, gentle lateral sway and visible plate rotation. Wind
            // contributes air velocity, never an unbounded horizontal acceleration.
            auto& snow = define(Id::LightSnow, "light_snow", "Neige légère");
            snow.particles = particles(Shape::Flake, 1536, {23,24,23}, {0,8,0},
                {.012f,-1,.006f}, .25f, .65f, 28, 44, 0.035f, 0.11f, {0.89f, 0.94f, 1.0f, 0.72f});
            snow.particles.group.flags |= Ge::RotateSprite;

            snow.particles.turbulence = .28f;
            snow.particles.turbulenceScale = .09f;
            snow.particles.windScale = .18f;
            snow.particles.group.rotateSprite = .65f;
            snow.particles.group.drag = .7f;
            snow.particles.group.spreadAngle = 9.f;

            auto& frost = define(Id::FrostRim, "frost_rim", "Givre au bord de l'écran");
            frost.screen.frost = .44f;
            frost.screen.scale = 1.1f;
            frost.screen.speed = .12f;
            frost.fadeIn = 12.0f;
            frost.fadeOut = 5.0f;

            auto& crystals = define(Id::IceCrystals, "ice_crystals", "Cristaux de glace");
            crystals.particles = particles(Shape::Crystal, 256, {18, 7, 18}, {0, 2, 0},
                {.04f,-1,.02f}, .06f, .2f, 20, 32, 0.025f, 0.055f, {0.64f, 0.86f, 1.0f, 0.66f});
            crystals.particles.group.flags |= Ge::RotateSprite;

            crystals.particles.emission = .35f;
            crystals.particles.pulse = .8f;
            crystals.particles.windScale = .15f;
            crystals.particles.turbulence = .07f;
            crystals.particles.turbulenceScale = .08f;
            crystals.particles.group.drag = .8f;
            crystals.particles.group.rotateSprite = .4f;

            auto& drift = define(Id::GroundSnowDrift, "ground_snow_drift", "Poudrerie au ras du sol");
            drift.particles = particles(Shape::Mist, 80, {28, 0.65f, 28}, {0, -1.15f, 0},
                {1, 0.02f, 0.2f}, .8f, 1.8f, 12, 20, .7f, 2.0f, {.80f,.89f,.95f,.045f});

            drift.particles.aspect = .18f;
            drift.particles.turbulence = .25f;
            drift.particles.turbulenceScale = .045f;
            drift.particles.windScale = .95f;
            drift.particles.group.drag = .7f;

            auto& leaves = define(Id::WindLeaves, "wind_leaves", "Feuilles emportées");
            leaves.particles = particles(Shape::Leaf, 192, {22, 7, 22}, {0, 3, 0},
                {.12f,-1,.08f}, .25f, .7f, 18, 30, 0.07f, 0.18f, {0.35f, 0.40f, 0.16f, 0.80f});
            leaves.particles.aspect = 1.65f;
            leaves.particles.group.flags |= Ge::RotateSprite;
            leaves.particles.group.rotateSprite = 1.4f;
            leaves.particles.turbulence = 0.75f;

            leaves.particles.group.gravity = V3(0,-.03f,0);
            leaves.particles.turbulenceScale = .16f;
            leaves.particles.windScale = .35f;
            leaves.particles.group.drag = .65f;

            auto& petals = define(Id::Petals, "petals", "Pétales flottants");
            petals.particles = particles(Shape::Petal, 208, {19, 5, 19}, {0, 2, 0},
                {.08f,-1,.05f}, .12f, .32f, 20, 34, 0.045f, 0.12f, {0.82f, 0.43f, 0.47f, 0.72f});
            petals.particles.group.flags |= Ge::RotateSprite;
            petals.particles.turbulence = 0.55f;
            petals.particles.group.rotateSprite = 0.85f;
            petals.particles.aspect = 1.25f;

            petals.particles.windScale = .22f;
            petals.particles.turbulenceScale = .12f;
            petals.particles.group.drag = .8f;
            petals.particles.group.spreadAngle = 24.f;

            auto& seeds = define(Id::DrySeeds, "dry_seeds", "Graines et aigrettes sèches");
            seeds.particles = particles(Shape::Seed, 384, {22, 3, 22}, {0, 0.5f, 0},
                {.12f,-.14f,.08f}, .08f, .3f, 16, 28, 0.045f, 0.10f, {0.62f, 0.57f, 0.38f, 0.60f});
            seeds.particles.aspect = 1.8f;

            seeds.particles.turbulence = .42f;
            seeds.particles.turbulenceScale = .2f;
            seeds.particles.windScale = .5f;
            seeds.particles.group.flags |= Ge::RotateSprite;
            seeds.particles.group.rotateSprite = 2.4f;
            seeds.particles.group.drag = .65f;
            seeds.particles.group.spreadAngle = 60.f;

            auto& motes = define(Id::DustMotes, "dust_motes", "Poussières en suspension");
            motes.particles = particles(Shape::Dust, 384, {17, 6, 17}, {0, 1, 0},
                {.1f,.12f,.05f}, .01f, .06f, 24, 40, 0.012f, 0.035f, {0.66f, 0.61f, 0.48f, 0.46f});

            motes.particles.windScale = .08f;
            motes.particles.turbulence = .12f;
            motes.particles.turbulenceScale = .035f;
            motes.particles.group.drag = 1.2f;
            motes.particles.group.spreadAngle = 160.f;

            auto& mist = define(Id::GroundMist, "ground_mist", "Nappes de brume basse");
            mist.particles = particles(Shape::Mist, 40, {28, 0.55f, 28}, {0, -1.2f, 0},
                {1, 0.01f, 0.1f}, .04f, .14f, 28, 46, 1.8f, 5.0f, {.52f,.61f,.64f,.035f});

            mist.particles.aspect = .22f;
            mist.particles.windScale = .12f;
            mist.particles.turbulence = .12f;
            mist.particles.turbulenceScale = .018f;
            mist.particles.group.drag = .25f;

            auto& swamp = define(Id::SwampVapor, "swamp_vapor", "Vapeurs de marécage");
            swamp.particles = particles(Shape::Mist, 48, {23, 0.7f, 23}, {0, -1, 0},
                {.03f,1,.02f}, .08f, .22f, 20, 32, .6f, 1.8f, {.34f,.46f,.35f,.05f});

            swamp.particles.turbulence = .4f;
            swamp.particles.turbulenceScale = .055f;
            swamp.particles.windScale = .08f;
            swamp.particles.group.drag = .45f;

            auto& spray = define(Id::SeaSpray, "sea_spray", "Embruns marins");
            spray.particles = particles(Shape::Droplet, 896, {23, 2.5f, 23}, {0, -0.7f, 0},
                {.55f,1,.12f}, 1.5f, 4.5f, 4, 8, 0.022f, 0.065f, {0.70f, 0.87f, 0.89f, 0.45f});
            spray.particles.aspect = 1.4f;
            spray.particles.turbulence = 0.3f;

            spray.particles.group.gravity = V3(0,-1.8f,0);
            spray.particles.windScale = .35f;
            spray.particles.group.drag = .12f;
            spray.particles.group.spreadAngle = 55.f;

            auto& salt = define(Id::SaltMist, "salt_mist", "Voiles salins");
            salt.particles = particles(Shape::Mist, 40, {28, 2, 28}, {0, -0.7f, 0},
                {1, 0.02f, 0.3f}, .12f, .32f, 22, 36, 1.2f, 3.2f, {.55f,.73f,.77f,.025f});

            salt.particles.aspect = .35f;
            salt.particles.windScale = .45f;
            salt.particles.turbulence = .32f;
            salt.particles.turbulenceScale = .035f;
            salt.particles.group.drag = .45f;

            auto& embers = define(Id::VolcanicEmbers, "volcanic_embers", "Braises volcaniques");
            embers.particles = particles(Shape::Ember, 192, {21, 2, 21}, {0, -0.7f, 0},
                {.04f,1,.025f}, .35f, 1.2f, 9, 16, 0.025f, 0.07f, {1.0f, 0.20f, 0.025f, 0.85f});
            embers.particles.emission = 4.0f;
            embers.particles.pulse = 3.2f;

            embers.particles.turbulence = .85f;
            embers.particles.turbulenceScale = .12f;
            embers.particles.windScale = .22f;
            embers.particles.group.drag = .4f;
            embers.particles.group.spreadAngle = 22.f;

            auto& fumes = define(Id::Fumaroles, "fumaroles", "Fumerolles");
            fumes.particles = particles(Shape::Mist, 40, {22, 0.25f, 22}, {0, -1.5f, 0},
                {0.02f, 1, 0.02f}, .35f, .8f, 18, 30, .7f, 2.2f, {.34f,.31f,.26f,.065f});

            fumes.particles.aspect = 1.65f;
            fumes.particles.turbulence = .65f;
            fumes.particles.turbulenceScale = .055f;
            fumes.particles.windScale = .12f;
            fumes.particles.group.drag = .3f;

            auto& spores = define(Id::Spores, "spores", "Spores flottantes");
            spores.particles = particles(Shape::Spore, 384, {18, 5, 18}, {0, 1.0f, 0},
                {0.15f, 1, 0.1f}, 0.035f, 0.15f, 13, 22, 0.025f, 0.065f, {0.52f, 0.58f, 0.34f, 0.58f});

            spores.particles.windScale = .10f;
            spores.particles.turbulence = .3f;
            spores.particles.turbulenceScale = .15f;
            spores.particles.group.drag = .8f;
            spores.particles.group.spreadAngle = 110.f;

            auto& glowing = define(Id::GlowingSpores, "glowing_spores", "Spores bioluminescentes");
            glowing.particles = particles(Shape::Spore, 128, {16, 4, 16}, {0, 0.6f, 0},
                {0.1f, 1, 0.2f}, 0.06f, 0.18f, 10, 18, 0.03f, 0.075f, {0.18f, 0.66f, 0.83f, 0.70f});
            glowing.particles.emission = 2.3f;
            glowing.particles.pulse = 0.7f;

            glowing.particles.windScale = .07f;
            glowing.particles.turbulence = .45f;
            glowing.particles.turbulenceScale = .1f;
            glowing.particles.group.drag = .7f;
            glowing.particles.group.spreadAngle = 100.f;

            auto& drips = define(Id::CanopyDrips, "canopy_drips", "Gouttes de la canopée");
            drips.particles = particles(Shape::Droplet, 384, {20,8,20}, {0,12,0},
                {0, -1, 0}, 1.0f, 2.4f, 2.5f, 5, 0.018f, 0.04f, {0.58f, 0.73f, 0.68f, 0.50f});
            drips.particles.aspect = 3.5f;
            drips.particles.windScale = 0.12f;
            drips.particles.group.gravity = V3(0, -2.5f, 0);
            drips.particles.group.flags |= Ge::VelOrientation;

            drips.particles.group.spreadAngle = 5.f;
            drips.particles.group.drag = .15f;

            auto& insects = define(Id::Insects, "insects", "Insectes en vol");
            insects.particles = particles(Shape::Insect, 64, {14, 2.7f, 14}, {0, 0.2f, 0},
                {0.4f, 0.2f, 0.3f}, 0.1f, 0.5f, 8, 14, 0.02f, 0.045f, {0.14f, 0.17f, 0.10f, 0.85f});
            insects.particles.pulse = 8.0f;

            insects.particles.turbulence = 1.1f;
            insects.particles.turbulenceScale = .45f;
            insects.particles.windScale = .02f;
            insects.particles.group.drag = 1.8f;
            insects.particles.group.spreadAngle = 160.f;

            auto& streaks = define(Id::WindStreaks, "wind_streaks", "Filets de vent");
            streaks.particles = particles(Shape::Streak, 96, {25, 4, 25}, {0, 0.2f, 0},
                {1, 0.03f, 0.2f}, 2.0f, 4.0f, 6.0f, 10.0f, 0.025f, 0.06f, {0.64f, 0.70f, 0.68f, 0.13f});
            streaks.particles.group.flags |= Ge::VelOrientation;

            streaks.particles.aspect = 14.f;
            streaks.particles.windScale = .8f;
            streaks.particles.turbulence = .6f;
            streaks.particles.turbulenceScale = .04f;

            // Third-person environment: extended suspended ice veils, no mouth
            // anchor, camera-facing puff or breathing pulse.
            auto& iceHaze = define(Id::IceHaze, "ice_haze", "Voiles de glace en suspension");
            iceHaze.particles = particles(Shape::Mist, 64, {24,2.5f,24}, {0,-.3f,0},
                {.12f,-.04f,.08f}, .025f, .08f, 18, 32, 2.0f, 6.0f, {.69f,.81f,.91f,.028f});

            iceHaze.particles.aspect = .14f;
            iceHaze.particles.pulse = 0.f;
            iceHaze.particles.windScale = .12f;
            iceHaze.particles.turbulence = .12f;
            iceHaze.particles.turbulenceScale = .025f;
            iceHaze.particles.group.drag = .2f;
            iceHaze.fadeIn = 4.f;
            iceHaze.fadeOut = 3.f;

            auto& wet = define(Id::WetLens, "wet_lens", "Gouttelettes sur l'écran");
            wet.screen.wet = 0.16f;
            wet.screen.scale = 1.1f;
            wet.screen.speed = 0.25f;
            wet.fadeIn = 3.0f;
            wet.fadeOut = 2.0f;

            auto& condensation = define(Id::Condensation, "condensation", "Buée périphérique");
            condensation.screen.condensation = 0.14f;
            condensation.screen.scale = 0.8f;
            condensation.screen.speed = 0.12f;
            condensation.fadeIn = condensation.fadeOut = 3.0f;

            auto& veil = define(Id::SandVeil, "sand_veil", "Voile de poussière chaude");
            veil.screen.dust = 0.10f;
            veil.screen.tint = V3(0.62f, 0.38f, 0.19f);
            veil.screen.scale = 0.65f;
            veil.screen.speed = 0.45f;

            auto& rain = define(Id::Rain, "rain", "Pluie", true);
            rain.particles = particles(Shape::Streak, 3072, {28,32,28}, {0,14,0},
                {0.08f, -1, 0.03f}, 7, 11, 2.5f, 4.5f, 0.018f, 0.035f, {0.58f, 0.69f, 0.76f, 0.42f});
            rain.particles.aspect = 14.0f;
            rain.particles.group.flags |= Ge::VelOrientation;
            rain.particles.group.gravity = V3(0, -0.6f, 0);
            rain.screen.wet = 0.25f;

            rain.particles.windScale = .3f;
            rain.particles.group.drag = .35f;
            rain.particles.group.spreadAngle = 3.f;

            auto& heavy = define(Id::HeavyRain, "heavy_rain", "Pluie battante", true);
            heavy.particles = particles(Shape::Streak, 6144, {31,38,31}, {0,18,0},
                {0.3f, -1, 0.12f}, 12, 17, 2, 3.5f, 0.022f, 0.045f, {0.52f, 0.64f, 0.72f, 0.53f});
            heavy.particles.aspect = 18.0f;

            heavy.particles.group.flags |= Ge::VelOrientation;
            heavy.screen.wet = 0.56f;
            heavy.screen.condensation = 0.09f;
            heavy.screen.speed = 1.6f;

            heavy.particles.windScale = .65f;
            heavy.particles.group.drag = .45f;
            heavy.particles.group.spreadAngle = 6.f;

            auto& storm = define(Id::Sandstorm, "sandstorm", "Tempête de sable", true);
            storm.particles = particles(Shape::Dust, 6144, {32, 13, 32}, {0, 2, 0},
                {1, 0.08f, 0.25f}, 8, 15, 3.5f, 6, 0.1f, 0.32f, {0.53f, 0.34f, 0.16f, 0.43f});
            storm.particles.aspect = 2.5f;
            storm.particles.windScale = 3.5f;
            storm.screen.dust = 0.65f;
            storm.screen.heat = 0.10f;
            storm.screen.tint = V3(0.57f, 0.36f, 0.16f);
            storm.screen.speed = 2.0f;
            storm.fadeIn = 5.0f;

            storm.particles.turbulence = 2.6f;
            storm.particles.turbulenceScale = .025f;
            storm.particles.group.drag = .8f;
            storm.particles.group.spreadAngle = 28.f;

            auto& blizzard = define(Id::Blizzard, "blizzard", "Tempête de neige", true);
            blizzard.particles = particles(Shape::Flake, 6144, {30,32,30}, {0,10,0},
                {.65f,-1,.12f}, 1.5f, 3.5f, 12, 20, 0.045f, 0.15f, {0.85f, 0.92f, 1.0f, 0.74f});
            blizzard.particles.group.flags |= Ge::RotateSprite;
            blizzard.particles.group.rotateSprite = 1.0f;
            blizzard.screen.frost = 0.55f;
            blizzard.screen.dust = 0.30f;
            blizzard.screen.tint = V3(0.70f, 0.81f, 0.91f);
            blizzard.screen.speed = 1.7f;
            blizzard.fadeIn = 4.0f;

            blizzard.particles.turbulence = 1.4f;
            blizzard.particles.turbulenceScale = .05f;
            blizzard.particles.windScale = 1.3f;
            blizzard.particles.group.drag = .65f;

            auto& ashStorm = define(Id::AshStorm, "ash_storm", "Tempête de cendres", true);
            ashStorm.particles = particles(Shape::Ash, 4096, {30, 12, 30}, {0, 5, 0},
                {.5f,-1,.14f}, 1.2f, 3.5f, 16, 26, 0.04f, 0.13f, {0.25f, 0.22f, 0.20f, 0.72f});
            ashStorm.particles.group.flags |= Ge::RotateSprite;
            ashStorm.particles.group.rotateSprite = 0.9f;
            ashStorm.screen.dust = 0.50f;
            ashStorm.screen.tint = V3(0.24f, 0.20f, 0.17f);
            ashStorm.screen.speed = 1.4f;

            ashStorm.particles.turbulence = 1.5f;
            ashStorm.particles.turbulenceScale = .045f;
            ashStorm.particles.windScale = 1.1f;
            ashStorm.particles.group.drag = .75f;

            auto& thunder = define(Id::Thunderstorm, "thunderstorm", "Orage lumineux", true);
            thunder.particles = particles(Shape::Streak, 4608, {30,40,30}, {0,18,0},
                {0.22f, -1, 0.18f}, 10, 15, 2.2f, 4, 0.02f, 0.04f, {0.48f, 0.60f, 0.70f, 0.47f});
            thunder.particles.aspect = 16.0f;

            thunder.particles.group.flags |= Ge::VelOrientation;
            thunder.screen.wet = 0.40f;
            thunder.screen.flash = 0.65f;
            thunder.screen.speed = 1.1f;

            thunder.particles.windScale = .9f;
            thunder.particles.turbulence = .3f;
            thunder.particles.group.drag = .6f;
            thunder.particles.group.spreadAngle = 9.f;

            auto& hail = define(Id::Hail, "hail", "Grêle", true);
            hail.particles = particles(Shape::Crystal, 2048, {27,36,27}, {0,16,0},
                {0.08f, -1, 0.03f}, 8, 13, 2.5f, 4.5f, 0.035f, 0.085f, {0.71f, 0.85f, 0.92f, 0.84f});
            hail.particles.group.gravity = V3(0, -1.5f, 0);
            hail.particles.aspect = 1.2f;
            hail.particles.group.flags |= Ge::RotateSprite;
            hail.screen.wet = 0.18f;
            hail.particles.group.rotateSprite = 1.2f;

            hail.particles.windScale = .08f;
            hail.particles.group.drag = .10f;
            hail.particles.group.spreadAngle = 5.f;

            auto& sleet = define(Id::Sleet, "sleet", "Grésil", true);
            sleet.particles = particles(Shape::Streak, 3584, {28,30,28}, {0,14,0},
                {0.28f, -1, 0.12f}, 4, 8, 3, 5.5f, 0.025f, 0.07f, {0.67f, 0.80f, 0.87f, 0.64f});
            sleet.particles.aspect = 4.5f;

            sleet.particles.group.flags |= Ge::VelOrientation;
            sleet.screen.wet = 0.26f;
            sleet.screen.frost = 0.13f;

            sleet.particles.windScale = .55f;
            sleet.particles.turbulence = .2f;
            sleet.particles.group.drag = .45f;
            sleet.particles.group.spreadAngle = 8.f;

            auto& meteors = define(Id::MeteorShower, "meteor_shower", "Pluie de fragments incandescents", true);
            meteors.particles = particles(Shape::Meteor, 32, {220,180,220}, {0,500,0},
                {0.65f, -1, 0.25f}, 30, 55, 8, 16, .25f, .65f, {1.0f, 0.45f, 0.12f, 0.90f});
            meteors.particles.aspect = 22.0f;
            meteors.particles.emission = 6.0f;
            meteors.particles.group.flags |= Ge::VelOrientation;
            meteors.particles.windScale = 0.0f;
            meteors.particles.group.drag = 0.0f;

            auto& lunar = define(Id::LunarDustFall, "lunar_dust_fall", "Retombées de poussière lunaire", true);
            lunar.particles = particles(Shape::Crystal, 1280, {31,34,31}, {0,12,0},
                {.025f,-1,.02f}, .18f, .55f, 28, 44, 0.035f, 0.12f, {0.46f, 0.49f, 0.58f, 0.65f});
            lunar.particles.group.flags |= Ge::RotateSprite;
            lunar.particles.emission = 0.12f;
            lunar.screen.dust = 0.12f;
            lunar.screen.tint = V3(0.40f, 0.43f, 0.52f);
            lunar.screen.speed = 0.25f;
            lunar.fadeIn = 4.0f;

            lunar.particles.turbulence = .2f;
            lunar.particles.windScale = .12f;
            lunar.particles.group.drag = .6f;
            lunar.particles.group.rotateSprite = .3f;

            auto& pollenBurst = define(Id::PollenBurst, "pollen_burst", "Envol massif de pollen", true);
            pollenBurst.particles = particles(Shape::Pollen, 2048, {26, 6, 26}, {0, 1, 0},
                {.18f,.6f,.12f}, .25f, .65f, 18, 30, 0.025f, 0.085f, {0.75f, 0.67f, 0.26f, 0.65f});
            pollenBurst.screen.dust = 0.07f;
            pollenBurst.screen.tint = V3(0.65f, 0.59f, 0.24f);

            pollenBurst.particles.turbulence = .95f;
            pollenBurst.particles.turbulenceScale = .05f;
            pollenBurst.particles.windScale = .7f;
            pollenBurst.particles.group.drag = .8f;
            pollenBurst.particles.group.spreadAngle = 85.f;

            auto& bloom = define(Id::SporeBloom, "spore_bloom", "Floraison de spores lumineuses", true);
            bloom.particles = particles(Shape::Spore, 960, {24, 7, 24}, {0, 1, 0},
                {0.1f, 1, 0.1f}, 0.15f, 0.45f, 10, 18, 0.035f, 0.11f, {0.27f, 0.72f, 0.59f, 0.68f});
            bloom.particles.group.nbColor = 4;
            bloom.particles.group.colorGradient[0] = V4(0.16f, 0.52f, 0.65f, 0.0f);
            bloom.particles.group.colorGradient[1] = V4(0.24f, 0.68f, 0.50f, 0.65f);
            bloom.particles.group.colorGradient[2] = V4(0.52f, 0.28f, 0.72f, 0.55f);
            bloom.particles.group.colorGradient[3] = V4(0.16f, 0.52f, 0.65f, 0.0f);
            bloom.particles.emission = 2.8f;
            bloom.particles.pulse = 0.9f;

            bloom.particles.turbulence = .8f;
            bloom.particles.turbulenceScale = .075f;
            bloom.particles.windScale = .12f;
            bloom.particles.group.drag = .6f;
            bloom.particles.group.spreadAngle = 110.f;
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
            {mask({Id::LightSnow,Id::FrostRim,Id::IceCrystals,Id::IceHaze}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::GroundSnowDrift,Id::IceCrystals,Id::WindStreaks}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::DrySeeds,Id::LightSnow,Id::IceHaze}), mask({Id::Blizzard,Id::Sleet,Id::Hail,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::LightSnow,Id::WindLeaves,Id::DustMotes}), mask({Id::Rain,Id::Blizzard,Id::Sleet,Id::PollenBurst,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::GroundMist,Id::Spores,Id::Condensation}), mask({Id::Rain,Id::Sleet,Id::SporeBloom,Id::MeteorShower,Id::LunarDustFall})},
            {mask({Id::WindStreaks,Id::IceCrystals,Id::IceHaze}), mask({Id::Blizzard,Id::Hail,Id::Sleet,Id::MeteorShower,Id::LunarDustFall})},
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
