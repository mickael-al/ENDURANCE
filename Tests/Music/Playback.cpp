#include "AudioManager.hpp"
#include "EnduranceMusicDirector.hpp"

#include <cmath>
#include <iostream>

namespace
{
using namespace EnduranceMusic;
const std::string Key = "integration/music";

// Both scheduler and asynchronous playback manager are production code. Only
// device/source operations and the engine clock use the existing audio fake.
struct PlaybackFixture
{
    std::vector<Track> tracks{
        {"forest", "forest.wav", "forest", Verdant, Role::Exploration, 0.7f},
        {"desert", "desert.wav", "desert", Vast, Role::Exploration, 0.6f},
        {"moon", "moon.wav", "moon", Lunar, Role::Lunar, 0.8f}
    };
    Ge::SoundManager sounds;
    FixtureTime time;
    std::unique_ptr<AudioManager> audio;
    Director director{tracks, 23u};
    int biome = 12;
    bool sawReleaseThenPlay = false;

    PlaybackFixture()
    {
        Engine::services = {&sounds, &time};
        audio = std::make_unique<AudioManager>();
        Settings settings;
        settings.biomeHoldSeconds = 0.2f;
        settings.minPlaySeconds = 0.4f;
        settings.fadeInSeconds = 0.3f;
        settings.fadeOutSeconds = 0.3f;
        settings.gapMinSeconds = settings.gapMaxSeconds = 0.0f;
        director.configure(settings);
    }

    ~PlaybackFixture()
    {
        apply(director.stop());
        audio.reset();
        assert(sounds.sourceCount() == 0 && sounds.bufferCount() == 0);
        Engine::services = {};
    }

    void apply(const Actions& actions)
    {
        if (actions.count == 2 && actions.commands[0].type == CommandType::Release &&
            actions.commands[1].type == CommandType::Play) sawReleaseThenPlay = true;
        for (std::size_t i = 0; i < actions.count; ++i)
        {
            const Command& command = actions.commands[i];
            switch (command.type)
            {
            case CommandType::Release:
                audio->releaseSound(Key);
                break;
            case CommandType::FadeOut:
                audio->stopFade(Key, command.seconds);
                break;
            case CommandType::Play:
                audio->registerSound(Key, {tracks[command.track].path});
                audio->play(Key, AudioPlayType::ONCE, SoundType::MUSIC, 0, 0.0f);
                audio->setVolume(Key, tracks[command.track].gain, command.seconds);
                break;
            }
        }
    }

    void tick()
    {
        if (sounds.isPaused()) return; // Same guard as the production behaviour.
        audio->update();
        const auto status = audio->playbackStatus(Key);
        Playback state = Playback::Stopped;
        switch (status.state)
        {
        case AudioPlaybackState::LOADING: state = Playback::Loading; break;
        case AudioPlaybackState::PLAYING: state = Playback::Playing; break;
        case AudioPlaybackState::PAUSED: state = Playback::Paused; break;
        default: break;
        }
        apply(director.update(time.getDeltaTime(), biome, {state, status.remainingSeconds}));
        assert(sounds.sourceCount() <= 1 && sounds.bufferCount() <= 1);
    }

    template<class Predicate> void until(Predicate condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition())
        {
            assert(std::chrono::steady_clock::now() < deadline);
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool playing(std::size_t track)
    {
        return director.currentTrack() == track &&
            audio->playbackStatus(Key).state == AudioPlaybackState::PLAYING;
    }
};
}

int main()
{
    {
        PlaybackFixture f;
        f.until([&] { return f.playing(0); });
        for (int i = 0; i < 4; ++i) f.tick();
        assert(std::abs(f.sounds.gain("forest.wav") - 0.7f) < 0.001f);

        assert(f.director.request(Role::Lunar, "moon/first-fragment", 100, 180.0f, true));
        f.tick(); // Queues the production fade; the old piece still exists.
        assert(f.director.currentTrack() == 0);
        const float before = f.sounds.gain("forest.wav");
        f.tick();
        assert(f.sounds.gain("forest.wav") < before);

        // A global pause cannot complete a pending transition or start its cue.
        f.sounds.paused = true;
        const float pausedGain = f.sounds.gain("forest.wav");
        for (int i = 0; i < 20; ++i) f.tick();
        assert(f.director.currentTrack() == 0);
        assert(f.sounds.gain("forest.wav") == pausedGain);
        f.sounds.paused = false;
        f.until([&] { return f.playing(2); });
        assert(f.sawReleaseThenPlay);
        assert(f.sounds.loads("moon.wav") == 1);
        assert(!f.director.request(Role::Lunar, "moon/first-fragment", 100, 0.0f, true));

        // Simulate the device reporting completion, then use the actual
        // Release+Play command sequence with the very same AudioManager key.
        f.audio->stop(Key);
        f.until([&] { return f.playing(0); });
        assert(f.sounds.loads("forest.wav") == 2);
        f.biome = 15;
        f.until([&] { return f.playing(1); });
        assert(f.sounds.loads("desert.wav") == 1);
        assert(f.sounds.bufferCount() == 1);

        f.apply(f.director.stop());
        assert(f.sounds.sourceCount() == 0 && f.sounds.bufferCount() == 0);
    }
    {
        PlaybackFixture f;
        f.biome = -1; // A narrative event can play without exploration climate.
        f.sounds.block("moon.wav");
        assert(f.director.request(Role::Lunar, "moon/canceled", 100, 180.0f, true));
        f.tick();
        f.sounds.waitForLoad("moon.wav");
        f.apply(f.director.stop());
        assert(f.audio->playbackStatus(Key).state == AudioPlaybackState::STOPPED);
        // Once/cooldown is not consumed until actual playback. Canceling the
        // old load then requesting this same key again must not lose the new one.
        assert(f.director.request(Role::Lunar, "moon/canceled", 100, 180.0f, true));
        f.tick();
        f.sounds.unblock("moon.wav");
        f.until([&] { return f.playing(2); });
        assert(f.sounds.loads("moon.wav") == 2);
        assert(f.sounds.plays("moon.wav") == 1);
        assert(f.sounds.sourceCount() == 1 && f.sounds.bufferCount() == 1);
    }
    std::cout << "PASS production music director + AudioManager: priority fades, pause, same-key transitions, biome replacement, bounded buffers, canceled async cue retry\n";
}
