#include "AudioManager.hpp"

#include <cmath>
#include <iostream>

namespace
{
template<class Predicate>
void waitFor(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

void waitForPlaying(AudioManager& audio, const std::string& key)
{
    waitFor([&] {
        audio.update();
        return audio.playbackStatus(key).state == AudioPlaybackState::PLAYING;
    });
}

template<class Test>
void fixture(Test test)
{
    Ge::SoundManager sounds;
    FixtureTime time;
    Engine::services = { &sounds, &time };
    {
        AudioManager audio;
        test(audio, sounds);
    }
    assert(sounds.bufferCount() == 0);
    assert(sounds.sourceCount() == 0);
    Engine::services = {};
}
}

int main()
{
    // Stop during a blocked WAV decode must prevent late publication/playback.
    for (bool fade : { false, true }) fixture([&](AudioManager& audio, auto& sounds) {
        sounds.block("slow.wav");
        audio.registerSound("track", { "slow.wav" });
        audio.play("track");
        sounds.waitForLoad("slow.wav");
        if (fade) audio.stopFade("track", 2.0f);
        else audio.stop("track");
        assert(audio.playbackStatus("track").state == AudioPlaybackState::STOPPED);
        sounds.unblock("slow.wav");
        audio.stopThread();
        assert(sounds.bufferCount() == 0);
        assert(sounds.sourceCount() == 0);
    });

    // Reusing a key before the previous decode ends must not let its old request
    // replace the new sound or clear its LOADING state.
    fixture([](AudioManager& audio, auto& sounds) {
        sounds.block("old.wav");
        sounds.block("new.wav");
        audio.registerSound("track", { "old.wav" });
        audio.play("track");
        sounds.waitForLoad("old.wav");
        audio.releaseSound("track");
        audio.registerSound("track", { "new.wav" });
        audio.play("track");
        sounds.unblock("old.wav");
        sounds.waitForLoad("new.wav");
        assert(audio.playbackStatus("track").state == AudioPlaybackState::LOADING);
        assert(sounds.bufferCount() == 0);
        assert(sounds.sourceCount() == 0);
        sounds.unblock("new.wav");
        waitForPlaying(audio, "track");
        assert(sounds.plays("new.wav") == 1);
        audio.releaseSound("track");
        assert(sounds.bufferCount() == 0);
        assert(sounds.sourceCount() == 0);
        audio.play("track"); // release also removes registration.
        assert(audio.playbackStatus("track").state == AudioPlaybackState::STOPPED);
    });

    // LOOP tracks must stay stopped, including after a completed fade, rather
    // than restarting from updateNormal's end-of-buffer handling.
    for (int mode : { 0, 1, 2 }) fixture([&](AudioManager& audio, auto& sounds) {
        audio.registerSound("loop", { "loop.wav" });
        audio.play("loop", AudioPlayType::LOOP);
        waitForPlaying(audio, "loop");
        assert(sounds.looping("loop.wav")); // Native wrap starts on the first pass.
        if (mode == 0) audio.stop("loop");
        else audio.stopFade("loop", mode == 1 ? 0.0f : 0.5f);
        for (int i = 0; i < 30; ++i) audio.update();
        assert(audio.playbackStatus("loop").state == AudioPlaybackState::STOPPED);
        assert(sounds.plays("loop.wav") == 1);
        audio.setVolume("loop", 1.0f); // Changing gain is not an implicit play.
        assert(sounds.plays("loop.wav") == 1);
    });

    // Fade-in time starts with audible playback, never with the async load.
    fixture([](AudioManager& audio, auto& sounds) {
        sounds.block("fade.wav");
        audio.registerSound("track", { "fade.wav" });
        audio.play("track", AudioPlayType::ONCE, SoundType::MUSIC, 0, 0.0f);
        audio.setVolume("track", 0.8f, 1.0f);
        sounds.waitForLoad("fade.wav");
        for (int i = 0; i < 30; ++i) audio.update();
        sounds.paused = true;
        sounds.unblock("fade.wav");
        waitFor([&] { return sounds.sourceCount() == 1; });
        for (int i = 0; i < 30; ++i) audio.update();
        assert(sounds.plays("fade.wav") == 0);
        assert(audio.playbackStatus("track").state == AudioPlaybackState::LOADING);
        sounds.paused = false;
        waitForPlaying(audio, "track");
        assert(std::abs(sounds.gain("fade.wav") - 0.08f) < 0.001f);
        for (int i = 0; i < 10; ++i) audio.update();
        assert(std::abs(sounds.gain("fade.wav") - 0.8f) < 0.001f);
    });

    // Invalid/partially loaded segmented tracks must release what succeeded and
    // report STOPPED once; the worker must not retry failed files itself.
    fixture([](AudioManager& audio, auto& sounds) {
        audio.registerSound("missing", { "intro.wav", "missing.wav", "outro.wav" });
        audio.play("missing");
        sounds.waitForLoad("missing.wav");
        waitFor([&] { return audio.playbackStatus("missing").state == AudioPlaybackState::STOPPED; });
        for (int i = 0; i < 30; ++i) audio.update();
        assert(sounds.loads("missing.wav") == 1);
        assert(sounds.bufferCount() == 0);
    });

    // Reused intro/loop/outro pointers are owned once, never released three times.
    fixture([](AudioManager& audio, auto& sounds) {
        audio.registerSound("triple", { "shared.wav", "shared.wav", "shared.wav" });
        audio.play("triple");
        waitForPlaying(audio, "triple");
        assert(sounds.bufferCount() == 1);
        audio.stop("triple");
        for (int i = 0; i < 30; ++i) audio.update();
        assert(sounds.plays("shared.wav") == 1);
        audio.releaseSound("triple");
        assert(sounds.bufferCount() == 0);
    });

    std::cout << "AudioManager lifecycle: all regressions passed\n";
}
