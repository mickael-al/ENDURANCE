#ifndef __AUDIO_MANAGER__
#define __AUDIO_MANAGER__

#include "ThreadQueue.hpp"
#include <unordered_map>
#include <unordered_set>
#include "PointeurClass.hpp"
#include <atomic>
#include <thread>
#include <glm/glm.hpp>

namespace Ge
{
    class AudioSource;
    class SoundBuffer;
}

using namespace Ge;

enum class AudioRequestType
{
    LOAD,
    STOP_THREAD
};

enum class AudioPlayType
{
    ONCE,
    LOOP,
    MULTI
};

enum class SoundType
{
    MUSIC,
    SFX    
};

enum class AudioPlaybackState { STOPPED, LOADING, PLAYING, PAUSED };

struct AudioPlaybackStatus
{
    AudioPlaybackState state = AudioPlaybackState::STOPPED;
    float remainingSeconds = 0.0f; // Finite ONCE playback, including later segments.
};

struct AudioRequest
{
    AudioRequestType type;
    std::string key;
    std::vector<std::string> paths;
    AudioPlayType pType;
    SoundType sType;
    unsigned int nb_multi;
    float volume = 1.0f;
};

struct AudioTrack
{
    std::unique_ptr<AudioSource, std::function<void(AudioSource*)>> source;

    SoundBuffer * first = nullptr;
    SoundBuffer * loop = nullptr;
    SoundBuffer * end = nullptr;
    unsigned char currentBuffer = 0;

    bool isTriple = false;
    bool loaded = false;
    bool startPending = false;
    float localVolume = 1.0f;
    float fadeStartVolume = 1.0f;
    float fadeTargetVolume = 1.0f;
    float fadeDuration = 0.0f;
    float fadeElapsed = 0.0f;
    bool fading = false;
    bool stopAfterFade = false;
    bool pauseAfterFade = false;
    AudioPlayType pType = AudioPlayType::ONCE;
    SoundType sType = SoundType::MUSIC;
    unsigned int nb_multi = 0;
};

class AudioManager
{
public:
    AudioManager();
    ~AudioManager();    
    void registerSound(const std::string& key, const std::vector<std::string>& paths);
    void play(const std::string& key, AudioPlayType pType = AudioPlayType::ONCE, SoundType sType = SoundType::MUSIC, unsigned int nb_multi = 0, float volume = 1.0f);
    void stop(const std::string& key);
    void stopFade(const std::string& key, float duration);
    void setVolume(const std::string& key, float volume, float transitionSeconds = 0.0f, bool pauseWhenSilent = false);
    void configureSpatial(
        const std::string& key,
        bool spatialized,
        const glm::vec3& position = glm::vec3(0.0f),
        float referenceDistance = 1.0f,
        float maximumDistance = 100.0f,
        float rolloffFactor = 1.0f);
    void setPosition(const std::string& key, const glm::vec3& position);
    void setMasterVolume(float volume);
    void update();
    float getMasterVolume() const;
    void stopThread();
    void musicStopper(std::string ingoreKey);
    bool musicState();
    AudioPlaybackStatus playbackStatus(const std::string& key) const;
private:
    void threadFunc();
    void loadSound(const std::string& key, const std::vector<std::string>& paths, AudioPlayType pType, SoundType sType, unsigned int nb_multi, float volume);
    void beginFade(AudioTrack& sound, float targetVolume, float duration, bool stopAfterFade, bool pauseAfterFade);
    void updateFade(AudioTrack& sound, float deltaTime);
    void updateTriple(AudioTrack& sound);
    void updateNormal(AudioTrack& sound);
private:
    const ptrClass* m_pc;
    std::unordered_map<std::string, std::vector<std::string>> m_registry;
    std::unordered_map<std::string, std::unique_ptr<AudioTrack>> m_sounds;
    std::unordered_set<std::string> m_pendingLoads;

    struct SpatialConfiguration
    {
        bool spatialized = false;
        glm::vec3 position = glm::vec3(0.0f);
        float referenceDistance = 1.0f;
        float maximumDistance = 100.0f;
        float rolloffFactor = 1.0f;
    };
    std::unordered_map<std::string, SpatialConfiguration> m_spatialConfigurations;

    struct VolumeConfiguration
    {
        float target = 1.0f;
        float transitionSeconds = 0.0f;
        bool pauseWhenSilent = false;
    };
    std::unordered_map<std::string, VolumeConfiguration> m_volumeConfigurations;

    ThreadQueue<AudioRequest> m_requests;
    std::thread m_worker;
    mutable std::mutex m_dataMutex;
    std::atomic<bool> m_stopping = false;
    float m_masterVolume = 1.0f;
    float m_soundEffectVolume = 1.0f;
};

#endif //!__AUDIO_MANAGER__
