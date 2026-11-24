#include "EnduranceAmbience.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace EnduranceAmbience;
namespace
{
std::string path(Clip clip) { return std::string("ambient/") + recordings()[static_cast<std::size_t>(clip)].file; }
struct Fixture
{
    Ge::SoundManager sounds;
    FixtureTime time;
    std::unique_ptr<Player> player;
    Settings settings;
    Weather weather;
    int biome = 15;
    Fixture()
    {
        Engine::services = {&sounds, &time};
        player = std::make_unique<Player>("ambient", 23u);
        settings.volume = 1.0f;
        settings.biomeHoldSeconds = .3f;
        settings.transitionSeconds = 1.0f;
    }
    ~Fixture()
    {
        player.reset();
        assert(sounds.bufferCount() == 0 && sounds.sourceCount() == 0);
        Engine::services = {};
    }
    void tick(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            player->update(.1f, biome, weather, settings);
            // A cancelled load may transiently own one unpublished source.
            assert(sounds.sourceCount() <= Player::MaxVoices + 1);
        }
    }
    template<class Predicate> void until(Predicate done)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done())
        {
            assert(std::chrono::steady_clock::now() < deadline);
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void bed(Clip clip)
    { until([&] { return sounds.plays(path(clip)) > 0; }); tick(15); }
};
}

int main()
{
    // All biome IDs map to a bed. Weather/destruction never runs as a passive bed.
    assert(recordings().size() == 17 && biomeProfiles().size() == 26);
    for (const auto& profile : biomeProfiles())
    {
        assert(profile.bed != Clip::None && profile.bedGain > 0.0f && profile.bedGain <= 1.0f);
        assert(profile.bed != Clip::Rain && profile.bed != Clip::DestructiveWind);
        assert(profile.bed != Clip::GalacticWind && profile.bed != Clip::ThunderClap);
    }
    {
        Fixture f;
        f.tick(2);
        assert(f.sounds.sourceCount() == 0); // Biome stability gate.
        f.bed(Clip::DesertWind);
        assert(f.player->stableBiome() == 15 && f.sounds.looping(path(Clip::DesertWind)));
        const float original = f.sounds.gain(path(Clip::DesertWind));
        f.biome = 21; // Same recording, different biome gain: no restart/redecode.
        f.tick(25);
        assert(f.sounds.loads(path(Clip::DesertWind)) == 1);
        assert(f.sounds.plays(path(Clip::DesertWind)) == 1);
        assert(f.sounds.gain(path(Clip::DesertWind)) > original);
        f.sounds.block(path(Clip::Coast));
        f.biome = 25;
        f.tick(5);
        f.sounds.waitForLoad(path(Clip::Coast));
        f.tick(40);
        assert(f.sounds.gain(path(Clip::DesertWind)) > .6f); // No hole while decoding.
        f.sounds.unblock(path(Clip::Coast));
        f.until([&] { return f.sounds.plays(path(Clip::Coast)) > 0; });
        f.tick(3);
        assert(f.sounds.gain(path(Clip::DesertWind)) > 0 && f.sounds.gain(path(Clip::Coast)) > 0);
        f.biome = 21; // Reverse an active crossfade, reusing both sources.
        f.tick(25);
        assert(f.sounds.sourceCount() == 1 && f.sounds.loads(path(Clip::DesertWind)) == 1);
        f.sounds.paused = true;
        const auto stable = f.player->stableBiome();
        f.biome = 0; f.tick(100);
        assert(f.player->stableBiome() == stable);
        f.sounds.paused = false;
        f.biome = -1; f.tick(60);
        assert(f.sounds.sourceCount() == 0); // Stale/teleported sample retires old ambience.
    }
    {
        Fixture f;
        f.bed(Clip::DesertWind);
        f.sounds.block(path(Clip::Coast));
        f.biome = 25; f.tick(5);
        f.sounds.waitForLoad(path(Clip::Coast));
        f.biome = 15; f.tick(5);
        f.sounds.unblock(path(Clip::Coast));
        f.tick(15);
        assert(f.sounds.plays(path(Clip::Coast)) == 0); // Superseded loading target stays silent.
    }
    {
        Fixture f;
        f.settings.biomeHoldSeconds = 0.0f;
        f.bed(Clip::DesertWind);
        f.sounds.block(path(Clip::Coast));
        f.biome = 25; f.tick();
        f.sounds.waitForLoad(path(Clip::Coast));
        f.sounds.unblock(path(Clip::Coast));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (f.sounds.sourceCount() != 2)
        {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        // The decode completes just as a third biome replaces it. Its silent
        // source must not permanently occupy the second crossfade slot.
        f.biome = 0; f.bed(Clip::MountainWind);
        assert(f.sounds.sourceCount() == 1);
    }
    {
        Fixture f;
        f.sounds.fail(path(Clip::DesertWind));
        f.bed(Clip::BlusteryWind);
        f.tick(400);
        assert(f.player->unavailable(Clip::DesertWind));
        assert(f.sounds.loads(path(Clip::DesertWind)) == 1); // No retry storm.
        f.settings.volume = .2f;
        f.tick();
        assert(std::abs(f.sounds.gain(path(Clip::BlusteryWind)) - .12f) < .001f);
        f.settings.volume = (std::numeric_limits<float>::quiet_NaN)();
        f.tick();
        assert(std::abs(f.sounds.gain(path(Clip::BlusteryWind)) - .33f) < .001f);
    }
    {
        Fixture f;
        f.bed(Clip::DesertWind);
        f.tick(1000);
        assert(f.sounds.loads(path(Clip::Rain)) == 0);
        f.weather.rain = .8f;
        f.bed(Clip::Rain);
        assert(f.sounds.gain(path(Clip::DesertWind)) > .5f);
        assert(std::abs(f.sounds.gain(path(Clip::Rain)) - .8f * f.settings.weatherVolume) < .001f);
        f.weather.thunder = 1.0f;
        f.until([&] { return f.sounds.plays(path(Clip::ThunderClap)) > 0; });
        assert(!f.sounds.looping(path(Clip::ThunderClap)));
        f.sounds.finish(path(Clip::ThunderClap)); f.tick(400);
        f.until([&] { return f.sounds.plays(path(Clip::ThunderStrike)) > 0; });
        f.weather = {}; f.tick(60);
        assert(f.sounds.sourceCount() == 1 && f.sounds.plays(path(Clip::DesertWind)) == 1);
        // Stress quick third targets while fades are in flight: bounded voices.
        for (int i = 0; i < 100; ++i)
        {
            f.biome = i % 2 ? 25 : 12;
            f.weather.rain = i % 2 ? 1.0f : 0.0f;
            f.weather.blizzard = i % 2 ? 0.0f : 1.0f;
            f.tick(4);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        f.weather = {}; f.biome = 15; f.bed(Clip::DesertWind);
        f.tick(60);
        assert(f.sounds.sourceCount() == 1);
        f.player->stop(); f.tick(2);
        assert(f.sounds.sourceCount() == 0);
    }
    {
        Fixture f;
        f.biome = 0; f.bed(Clip::MountainWind);
        f.until([&] { return f.sounds.plays(path(Clip::IceCrack)) > 0; });
        assert(!f.sounds.looping(path(Clip::IceCrack)));
        f.sounds.finish(path(Clip::IceCrack)); f.tick();
        const auto loads = f.sounds.loads(path(Clip::IceCrack));
        f.tick(300);
        assert(f.sounds.loads(path(Clip::IceCrack)) == loads); // >=35 seconds between accents.
    }
    std::cout << "Ambient playback: biome gates, crossfades/reversal, cancellation, pause, failure, volumes, weather, cues and bounds passed\n";
}
