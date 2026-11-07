#include "AudioManager.hpp"
#define CLEAR_REGISTRY
#include "AudioSource.hpp"
#include "Debug.hpp"
#include "SoundBuffer.hpp"
#include "Engine.hpp"

#include <algorithm>

AudioManager::AudioManager()
{
    m_pc = Engine::getPtrClassAddr();
    m_worker = std::thread(&AudioManager::threadFunc, this);
}

AudioManager::~AudioManager()
{
    stopThread();

    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_sounds.clear();
    m_registry.clear();
    m_spatialConfigurations.clear();
    m_volumeConfigurations.clear();
}

void AudioManager::registerSound(const std::string& key, const std::vector<std::string>& paths)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_registry[key] = paths;
}

void AudioManager::play(const std::string& key, AudioPlayType pType, SoundType sType, unsigned int nb_multi, float volume)
{
    if (m_stopping.load())
    {
        return;
    }

    bool soundStarted = false;
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        auto soundIt = m_sounds.find(key);
        if (soundIt == m_sounds.end())
        {
            if (m_pendingLoads.count(key) != 0)
            {
                return;
            }
            auto registryIt = m_registry.find(key);
            if (registryIt == m_registry.end() || registryIt->second.empty())
            {
                return;
            }

            AudioRequest req;
            req.type = AudioRequestType::LOAD;
            req.key = key;
            req.paths = registryIt->second;
            req.pType = pType;
            req.sType = sType;
            req.nb_multi = nb_multi;
            req.volume = std::clamp(volume, 0.0f, 1.0f);
            m_pendingLoads.insert(key);
            m_requests.push(req);
            return;
        }

        auto& sound = soundIt->second;
        if (sound->loaded && sound->source)
        {
            sound->startPending = false;
            sound->fading = false;
            sound->stopAfterFade = false;
            sound->pauseAfterFade = false;
            sound->fadeElapsed = 0.0f;
            sound->fadeDuration = 0.0f;
            sound->localVolume = std::clamp(volume, 0.0f, 1.0f);
            sound->fadeTargetVolume = sound->localVolume;
            sound->pType = pType;
            sound->sType = sType;
            sound->nb_multi = nb_multi;
            sound->source->stop();
            sound->source->setLoop(false);
            sound->source->setGain(sound->localVolume * m_masterVolume);
            sound->source->setSoundBuffer(sound->first);
            sound->source->play();
            sound->currentBuffer = 0;
            soundStarted = true;
        }
    }

    if (soundStarted && sType == SoundType::MUSIC)
    {
        musicStopper(key);
    }
}

void AudioManager::musicStopper(std::string ingoreKey)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    for (auto& [key, sound] : m_sounds)
    {
        if (!sound->loaded || !sound->source || key == ingoreKey || sound->sType != SoundType::MUSIC)
        {
            continue;
        }

        SoundBuffer* current = nullptr;
        switch (sound->currentBuffer)
        {
        case 0: current = sound->first; break;
        case 1: current = sound->loop;  break;
        case 2: current = sound->end;   break;
        default: continue;
        }

        if (!sound->isTriple)
        {
            current = sound->first;
        }

        if (current != nullptr &&
            (sound->source->getTime() < current->getTime() || AL_STOPPED != sound->source->SourceState()))
        {
            beginFade(*sound, 0.0f, 1.0f, true, false);
        }
    }
}

bool AudioManager::musicState()
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    for (auto& [key, sound] : m_sounds)
    {
        if (!sound->loaded || !sound->source || sound->sType != SoundType::MUSIC)
        {
            continue;
        }

        SoundBuffer* current = nullptr;
        switch (sound->currentBuffer)
        {
        case 0: current = sound->first; break;
        case 1: current = sound->loop;  break;
        case 2: current = sound->end;   break;
        default: continue;
        }

        if (!sound->isTriple)
        {
            current = sound->first;
        }

        if (current != nullptr &&
            sound->source->getTime() < current->getTime() &&
            AL_STOPPED != sound->source->SourceState())
        {
            return true;
        }
    }
    return false;
}

AudioPlaybackStatus AudioManager::playbackStatus(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    const auto entry = m_sounds.find(key);
    if (entry == m_sounds.end() || !entry->second->loaded || !entry->second->source)
    {
        return { m_pendingLoads.count(key) != 0
            ? AudioPlaybackState::LOADING : AudioPlaybackState::STOPPED, 0.0f };
    }
    const AudioTrack& sound = *entry->second;
    if (sound.startPending)
    {
        return { AudioPlaybackState::LOADING, 0.0f };
    }
    const int state = sound.source->SourceState();
    if (state != AL_PLAYING && state != AL_PAUSED)
    {
        return {};
    }
    SoundBuffer* current = sound.first;
    float laterDuration = 0.0f;
    if (sound.isTriple)
    {
        switch (sound.currentBuffer)
        {
        case 0:
            if (sound.loop != nullptr) laterDuration += sound.loop->getTime();
            if (sound.end != nullptr) laterDuration += sound.end->getTime();
            break;
        case 1:
            current = sound.loop;
            if (sound.end != nullptr) laterDuration += sound.end->getTime();
            break;
        case 2: current = sound.end; break;
        default: return {};
        }
    }
    return { state == AL_PAUSED ? AudioPlaybackState::PAUSED : AudioPlaybackState::PLAYING,
        current == nullptr ? 0.0f : laterDuration +
            (std::max)(0.0f, static_cast<float>(current->getTime()) - sound.source->getTime()) };
}

void AudioManager::stop(const std::string& key)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);

    auto it = m_sounds.find(key);
    if (it != m_sounds.end() && it->second->source)
    {
        it->second->fading = false;
        it->second->startPending = false;
        it->second->stopAfterFade = false;
        it->second->pauseAfterFade = false;
        it->second->source->stop();
    }
}

void AudioManager::stopFade(const std::string& key, float duration)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);

    auto it = m_sounds.find(key);
    if (it != m_sounds.end())
    {
        beginFade(*it->second, 0.0f, duration, true, false);
    }
}

void AudioManager::setVolume(
    const std::string& key,
    float volume,
    float transitionSeconds,
    bool pauseWhenSilent)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_volumeConfigurations[key] = {
        std::clamp(volume, 0.0f, 1.0f),
        (std::max)(0.0f, transitionSeconds),
        pauseWhenSilent
    };
    auto it = m_sounds.find(key);
    if (it == m_sounds.end() || !it->second->source || it->second->startPending)
    {
        return;
    }

    AudioTrack& sound = *it->second;
    const float target = std::clamp(volume, 0.0f, 1.0f);
    if (target > 0.0f && !sound.source->isPlaying())
    {
        sound.source->play();
    }
    beginFade(
        sound,
        target,
        (std::max)(0.0f, transitionSeconds),
        false,
        pauseWhenSilent && target <= 0.0f);
}

void AudioManager::configureSpatial(
    const std::string& key,
    bool spatialized,
    const glm::vec3& position,
    float referenceDistance,
    float maximumDistance,
    float rolloffFactor)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    SpatialConfiguration& configuration = m_spatialConfigurations[key];
    configuration.spatialized = spatialized;
    configuration.position = position;
    configuration.referenceDistance = (std::max)(0.001f, referenceDistance);
    configuration.maximumDistance = (std::max)(
        configuration.referenceDistance,
        maximumDistance);
    configuration.rolloffFactor = (std::max)(0.0f, rolloffFactor);

    auto it = m_sounds.find(key);
    if (it == m_sounds.end() || !it->second->source)
    {
        return;
    }
    AudioSource& source = *it->second->source;
    source.setRelative(spatialized);
    source.setPosition(configuration.position);
    source.setRefDistance(configuration.referenceDistance);
    source.setMaxDistance(configuration.maximumDistance);
    source.setRolloffFactor(configuration.rolloffFactor);
}

void AudioManager::setPosition(
    const std::string& key,
    const glm::vec3& position)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_spatialConfigurations[key].position = position;
    auto it = m_sounds.find(key);
    if (it != m_sounds.end() && it->second->source)
    {
        it->second->source->setPosition(position);
    }
}

void AudioManager::setMasterVolume(float volume)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);

    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);

    for (auto& [key, sound] : m_sounds)
    {
        if (sound->source)
        {
            sound->source->setGain(sound->localVolume * m_masterVolume);
        }
    }
}

float AudioManager::getMasterVolume() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_masterVolume;
}

void AudioManager::stopThread()
{
    if (!m_stopping.exchange(true))
    {
        m_requests.shutdown();
    }

    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void AudioManager::threadFunc()
{
    AudioRequest req;

    while (m_requests.pop(req))
    {
        if (req.type == AudioRequestType::LOAD)
        {
            loadSound(req.key, req.paths,req.pType, req.sType, req.nb_multi, req.volume);
            std::lock_guard<std::mutex> lock(m_dataMutex);
            m_pendingLoads.erase(req.key);
        }
    }
}

void AudioManager::loadSound(const std::string& key, const std::vector<std::string>& paths, AudioPlayType pType, SoundType sType, unsigned int nb_multi, float volume)
{
    if (m_pc == nullptr || m_pc->soundManager == nullptr ||
        (paths.empty() || paths.size() > 3))
    {
        Debug::Warn(
            "AudioManager: invalid sound registration for %s (%llu path(s))",
            key.c_str(),
            static_cast<unsigned long long>(paths.size()));
        return;
    }

    auto sound = std::make_unique<AudioTrack>();
    const ptrClass* pc = m_pc;
    if (paths.size() == 1)
    {
        sound->first = m_pc->soundManager->createBuffer(paths[0].c_str());
    }
    else
    {
        sound->isTriple = true;

        sound->first = m_pc->soundManager->createBuffer(paths[0].c_str());
        sound->loop = paths[1] == paths[0]
            ? sound->first
            : m_pc->soundManager->createBuffer(paths[1].c_str());
        if (paths.size() == 3)
        {
            sound->end = paths[2] == paths[0]
                ? sound->first
                : paths[2] == paths[1]
                    ? sound->loop
                    : m_pc->soundManager->createBuffer(paths[2].c_str());
        }

    }

    SoundBuffer* first = sound->first;
    SoundBuffer* loop = sound->loop;
    SoundBuffer* end = sound->end;
    const bool buffersReady = first != nullptr &&
        (!sound->isTriple || (loop != nullptr && (paths.size() == 2 || end != nullptr)));
    if (!buffersReady)
    {
        if (first != nullptr) m_pc->soundManager->releaseBuffer(first);
        if (loop != nullptr && loop != first)
        {
            m_pc->soundManager->releaseBuffer(loop);
        }
        if (end != nullptr && end != first && end != loop)
        {
            m_pc->soundManager->releaseBuffer(end);
        }
        Debug::Warn("AudioManager: unable to load sound %s", key.c_str());
        return;
    }

    AudioSource* source = m_pc->soundManager->createSource(
        first,
        sType == SoundType::MUSIC ? "Music" : "Ambience",
        sType == SoundType::MUSIC);
    if (source == nullptr)
    {
        m_pc->soundManager->releaseBuffer(first);
        if (loop != nullptr && loop != first)
        {
            m_pc->soundManager->releaseBuffer(loop);
        }
        if (end != nullptr && end != first && end != loop)
        {
            m_pc->soundManager->releaseBuffer(end);
        }
        Debug::Warn("AudioManager: unable to create source for %s", key.c_str());
        return;
    }
    sound->source = std::unique_ptr<
        AudioSource,
        std::function<void(AudioSource*)>>(
            source,
            [pc, first, loop, end](AudioSource* audio)
            {
                if (pc == nullptr || pc->soundManager == nullptr)
                {
                    return;
                }
                pc->soundManager->releaseSource(audio);
                pc->soundManager->releaseBuffer(first);
                if (loop != nullptr && loop != first)
                {
                    pc->soundManager->releaseBuffer(loop);
                }
                if (end != nullptr && end != first && end != loop)
                {
                    pc->soundManager->releaseBuffer(end);
                }
            });

    sound->loaded = true;
    sound->pType = pType;
    sound->sType = sType;
    sound->nb_multi = nb_multi;
    sound->localVolume = std::clamp(volume, 0.0f, 1.0f);
    sound->fadeTargetVolume = sound->localVolume;
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        // A scene may have been closed while its WAV files were loading.
        if (m_stopping.load())
        {
            return;
        }
        sound->source->setGain(sound->localVolume * m_masterVolume);
        const auto configuration = m_spatialConfigurations.find(key);
        if (configuration != m_spatialConfigurations.end())
        {
            const SpatialConfiguration& spatial = configuration->second;
            sound->source->setRelative(spatial.spatialized);
            sound->source->setPosition(spatial.position);
            sound->source->setRefDistance(spatial.referenceDistance);
            sound->source->setMaxDistance(spatial.maximumDistance);
            sound->source->setRolloffFactor(spatial.rolloffFactor);
        }
        else
        {
            sound->source->setRelative(false);
        }
        // Start on the next main-thread update. This respects engine pause and
        // the latest gain without racing a scene change or global pause toggle.
        sound->startPending = true;
        m_sounds[key] = std::move(sound);
    }
    if (sType == SoundType::MUSIC) musicStopper(key);
}

void AudioManager::update()
{
    if (m_stopping.load() || m_pc == nullptr || m_pc->time == nullptr ||
        (m_pc->soundManager != nullptr && m_pc->soundManager->isPaused()))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_dataMutex);
    const float deltaTime = std::clamp(m_pc->time->getDeltaTime(), 0.0f, 0.1f);

    for (auto& [key, sound] : m_sounds)
    {
        if (!sound->loaded || !sound->source)
        {
            continue;
        }

        if (sound->startPending)
        {
            sound->startPending = false;
            sound->source->setGain(sound->localVolume * m_masterVolume);
            sound->source->play();
            const auto volume = m_volumeConfigurations.find(key);
            if (volume != m_volumeConfigurations.end())
            {
                const VolumeConfiguration& configuration = volume->second;
                beginFade(*sound, configuration.target, configuration.transitionSeconds,
                    false, configuration.pauseWhenSilent && configuration.target <= 0.0f);
            }
        }

        if (!sound->isTriple)
        {
            updateNormal(*sound);
        }
        else
        {
            updateTriple(*sound);
        }

        updateFade(*sound, deltaTime);
    }
}

void AudioManager::beginFade(
    AudioTrack& sound,
    float targetVolume,
    float duration,
    bool stopAfterFade,
    bool pauseAfterFade)
{
    if (!sound.loaded || !sound.source)
    {
        return;
    }

    const float target = std::clamp(targetVolume, 0.0f, 1.0f);
    if (duration <= 0.0f)
    {
        sound.fading = false;
        sound.localVolume = target;
        sound.fadeTargetVolume = target;
        sound.source->setGain(target * m_masterVolume);
        if (stopAfterFade && target <= 0.0f)
        {
            sound.source->stop();
        }
        else if (pauseAfterFade && target <= 0.0f)
        {
            sound.source->pause();
        }
        sound.stopAfterFade = false;
        sound.pauseAfterFade = false;
        return;
    }

    sound.fadeStartVolume = sound.localVolume;
    sound.fadeTargetVolume = target;
    sound.fadeDuration = duration;
    sound.fadeElapsed = 0.0f;
    sound.fading = true;
    sound.stopAfterFade = stopAfterFade;
    sound.pauseAfterFade = pauseAfterFade;
}

void AudioManager::updateFade(AudioTrack& sound, float deltaTime)
{
    if (!sound.fading || !sound.source)
    {
        return;
    }

    sound.fadeElapsed += deltaTime;
    const float fadePercent = std::clamp(sound.fadeElapsed / sound.fadeDuration, 0.0f, 1.0f);
    sound.localVolume = glm::mix(
        sound.fadeStartVolume,
        sound.fadeTargetVolume,
        fadePercent);
    sound.source->setGain(sound.localVolume * m_masterVolume);

    if (fadePercent >= 1.0f)
    {
        sound.fading = false;
        if (sound.stopAfterFade && sound.fadeTargetVolume <= 0.0f)
        {
            sound.source->stop();
        }
        else if (sound.pauseAfterFade && sound.fadeTargetVolume <= 0.0f)
        {
            sound.source->pause();
        }
        sound.stopAfterFade = false;
        sound.pauseAfterFade = false;
    }
}

void AudioManager::updateTriple(AudioTrack& sound)
{
    SoundBuffer* current = nullptr;

    switch (sound.currentBuffer)
    {
    case 0: current = sound.first; break;
    case 1: current = sound.loop;  break;
    case 2: current = sound.end;   break;
    default: return;
    }

    if (!current)
    {
        return;
    }

    float currentTime = sound.source->getTime();
    float duration = current->getTime();    
    if (currentTime >= duration || AL_STOPPED == sound.source->SourceState())
    {        
        if (sound.currentBuffer == 0)
        {          
            sound.source->setSoundBuffer(sound.loop);
            if (sound.pType == AudioPlayType::ONCE)
            {                
                sound.source->play();
                sound.currentBuffer = 1;
                currentTime = sound.source->getTime();
            }
            else if (sound.pType == AudioPlayType::MULTI)
            {                                
                if (sound.nb_multi > 0)
                {                   
                    sound.source->play();
                    sound.nb_multi--;
                }
                else
                {
                    sound.source->stop();
                    sound.currentBuffer = 1;
                }
            }
            else if (sound.pType == AudioPlayType::LOOP)
            {
                sound.source->setLoop(true);
                sound.source->play();
                sound.currentBuffer = 1;
            }
        }
        else if (sound.currentBuffer == 1)
        {
            // An intro + loop registered with ONCE ends after one loop pass.
            if (sound.end == nullptr)
            {
                sound.currentBuffer = 3;
                sound.source->stop();
                return;
            }
            currentTime = sound.source->getTime();
            sound.currentBuffer = 2;
            sound.source->setSoundBuffer(sound.end);
            sound.source->play();
        }
        else if (sound.currentBuffer == 2)
        {            
            sound.source->stop();
        }
    }
}

void AudioManager::updateNormal(AudioTrack& sound)
{
    float currentTime = sound.source->getTime();
    float duration = sound.first->getTime();
    if (currentTime >= duration || AL_STOPPED == sound.source->SourceState())
    {
        if (sound.pType == AudioPlayType::ONCE)
        {
            sound.source->stop();
        }
        else if (sound.pType == AudioPlayType::MULTI)
        {
            if (sound.nb_multi > 0)
            {
                sound.source->play();
                sound.nb_multi--;
            }
            else
            {
                sound.source->stop();
            }
        }
        else if (sound.pType == AudioPlayType::LOOP)
        {
            sound.source->setSoundBuffer(sound.first);
            sound.source->setLoop(true);
            sound.source->play();
        }
    }
}
