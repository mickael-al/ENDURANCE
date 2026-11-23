#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>

inline constexpr int AL_STOPPED = 0;
inline constexpr int AL_PLAYING = 1;
inline constexpr int AL_PAUSED = 2;
inline const std::thread::id audioFixtureMainThread = std::this_thread::get_id();

namespace Ge
{
class SoundBuffer
{
public:
    explicit SoundBuffer(std::string name) : path(std::move(name)) {}
    float getTime() const { return 30.0f; }
    const std::string path;
};

class AudioSource
{
public:
    explicit AudioSource(SoundBuffer* buffer) : buffer(buffer) {}
    void play() { state = AL_PLAYING; ++playCount; }
    void pause() { state = AL_PAUSED; }
    void stop() { state = AL_STOPPED; }
    bool isPlaying() const { return state == AL_PLAYING; }
    int SourceState() const { return state; }
    float getTime() const { return offset; }
    void setGain(float value)
    {
        // Real AudioSource caches both local/bus gains in ordinary floats.
        // AudioManager must not mutate that cache from its loading worker.
        assert(std::this_thread::get_id() == audioFixtureMainThread);
        gain = value;
    }
    void setLoop(bool value) { looping = value; }
    void setSoundBuffer(SoundBuffer* value) { buffer = value; offset = 0.0f; }
    void setRelative(bool) {}
    void setPosition(const glm::vec3&) {}
    void setRefDistance(float) {}
    void setMaxDistance(float) {}
    void setRolloffFactor(float) {}

    SoundBuffer* buffer;
    std::atomic<int> state{ AL_STOPPED };
    std::atomic<unsigned> playCount{ 0 };
    std::atomic<float> gain{ 1.0f };
    float offset = 0.0f;
    bool looping = false;
};

class SoundManager
{
public:
    SoundBuffer* createBuffer(const char* path)
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++loadCalls[path];
        changed.notify_all();
        changed.wait(lock, [&] { return blocked.count(path) == 0; });
        if (std::string(path).find("missing") == 0) return nullptr;
        auto* result = new SoundBuffer(path);
        buffers.insert(result);
        return result;
    }
    void releaseBuffer(SoundBuffer* value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* source : sources) assert(source->buffer != value);
        assert(buffers.erase(value) == 1);
        delete value;
    }
    AudioSource* createSource(SoundBuffer* buffer, const std::string&, bool)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto* result = new AudioSource(buffer);
        sources.insert(result);
        return result;
    }
    void releaseSource(AudioSource* value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        assert(sources.erase(value) == 1);
        delete value;
    }
    bool isPaused() const { return paused; }
    void block(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        blocked.insert(path);
    }
    void unblock(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        blocked.erase(path);
        changed.notify_all();
    }
    void waitForLoad(const std::string& path)
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return loadCalls[path] > 0; }));
    }
    unsigned loads(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return loadCalls[path];
    }
    std::size_t bufferCount()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return buffers.size();
    }
    std::size_t sourceCount()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return sources.size();
    }
    float gain(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* source : sources) if (source->buffer->path == path) return source->gain;
        assert(false);
        return -1.0f;
    }
    unsigned plays(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* source : sources) if (source->buffer->path == path) return source->playCount;
        return 0;
    }
    std::atomic<bool> paused{ false };
private:
    std::mutex mutex;
    std::condition_variable changed;
    std::unordered_set<std::string> blocked;
    std::unordered_map<std::string, unsigned> loadCalls;
    std::unordered_set<SoundBuffer*> buffers;
    std::unordered_set<AudioSource*> sources;
};
}

struct FixtureTime { float getDeltaTime() const { return 0.1f; } };
struct ptrClass { Ge::SoundManager* soundManager = nullptr; FixtureTime* time = nullptr; };
struct Engine
{
    static const ptrClass* getPtrClassAddr() { return &services; }
    inline static ptrClass services;
};
struct Debug { template<class... Args> static void Warn(Args&&...) {} };
