#include "EnduranceMusic.hpp"
#define CLEAR_REGISTRY
#include "EnduranceAtmosphere.hpp"
#include "EnduranceAmbience.hpp"
#include "EnduranceBiomeEffects.hpp"
#include "EnduranceMusicCatalog.hpp"
#include "EnduranceMusicDirector.hpp"
#include "AudioManager.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

REGISTER(Behaviour, EnduranceMusicComponent, enabled, musicRoot, catalogFile, volume,
    biomeHoldSeconds, minimumPlaySeconds, fadeInSeconds, fadeOutSeconds,
    silenceMinSeconds, silenceMaxSeconds, recentCompositions,
    ambienceEnabled, ambienceVolume, ambienceDetailVolume, weatherVolume, ambienceTransitionSeconds);

namespace
{
    EnduranceMusicComponent* activeMusic = nullptr;
    const std::string AudioKey = "endurance/biome-music";

    bool finiteFloat(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    float bounded(float value, float fallback, float lo, float hi)
    { return finiteFloat(value) ? std::clamp(value, lo, hi) : fallback; }

    bool editPath(const char* label, std::string& value)
    {
        std::array<char, 2048> buffer{};
        std::memcpy(buffer.data(), value.data(), (std::min)(value.size(), buffer.size() - 1));
        if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
        value = buffer.data();
        return true;
    }

    EnduranceMusic::PlaybackInfo playbackInfo(const AudioPlaybackStatus& status)
    {
        using EnduranceMusic::Playback;
        Playback state = Playback::Stopped;
        switch (status.state)
        {
        case AudioPlaybackState::LOADING: state = Playback::Loading; break;
        case AudioPlaybackState::PLAYING: state = Playback::Playing; break;
        case AudioPlaybackState::PAUSED: state = Playback::Paused; break;
        default: break;
        }
        return {state, status.remainingSeconds};
    }
}

struct EnduranceMusicComponent::Impl
{
    const ptrClass* pc = nullptr;
    EnduranceMusic::Catalog catalog;
    // The director references catalog.tracks: destroy it before replacing the catalog.
    std::unique_ptr<EnduranceMusic::Director> director;
    std::unique_ptr<AudioManager> audio;
    std::unique_ptr<EnduranceAmbience::Player> ambience;
    bool running = false, wasEnabled = false;
    float appliedVolume = -1.0f;
    int sampledBiome = -1;
    std::string error;

    void apply(const EnduranceMusic::Actions& actions)
    {
        if (!audio) return;
        for (std::size_t i = 0; i < actions.count; ++i)
        {
            const auto& command = actions.commands[i];
            switch (command.type)
            {
            case EnduranceMusic::CommandType::Play:
            {
                if (command.track >= catalog.tracks.size()) break;
                const auto& track = catalog.tracks[command.track];
                audio->registerSound(AudioKey, {track.path});
                // Non-spatial music; gain is also multiplied by SoundManager's music bus.
                audio->play(AudioKey, AudioPlayType::ONCE, SoundType::MUSIC, 0, 0.0f);
                audio->setVolume(AudioKey, track.gain, command.seconds);
                break;
            }
            case EnduranceMusic::CommandType::FadeOut:
                audio->stopFade(AudioKey, command.seconds);
                break;
            case EnduranceMusic::CommandType::Release:
                audio->releaseSound(AudioKey);
                break;
            }
        }
    }
};

EnduranceMusicComponent::EnduranceMusicComponent() : m(std::make_unique<Impl>()) {}
EnduranceMusicComponent::~EnduranceMusicComponent() { stop(); }
EnduranceMusicComponent* EnduranceMusicComponent::active() { return activeMusic; }

void EnduranceMusicComponent::start()
{
    stop();
    if (activeMusic && activeMusic != this)
    {
        m->error = "Another music director is already active in this scene.";
        return;
    }
    m->pc = Ge::Engine::getPtrClassAddr();
    if (!m->pc || !m->pc->soundManager || !m->pc->time)
    {
        m->error = "Music requires SoundManager and Time.";
        return;
    }
    sanitizeSettings();
    m->running = true;
    activeMusic = this;
    reloadCatalog();
}

void EnduranceMusicComponent::stop()
{
    if (m->director) m->apply(m->director->stop());
    m->audio.reset(); // cancel / join loader before the scene's SoundManager is released
    m->ambience.reset();
    m->director.reset();
    m->running = false;
    m->wasEnabled = false;
    m->sampledBiome = -1;
    m->pc = nullptr;
    if (activeMusic == this) activeMusic = nullptr;
}

void EnduranceMusicComponent::reloadCatalog()
{
    if (!m->running) return;
    m->ambience.reset();
    const auto seed = static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    m->ambience = std::make_unique<EnduranceAmbience::Player>(musicRoot + "/Ambiant", seed);
    if (m->director) m->apply(m->director->stop());
    m->audio.reset();
    m->director.reset();
    m->catalog = EnduranceMusic::loadCatalog(musicRoot, catalogFile);
    m->error.clear();
    m->wasEnabled = false;
    if (m->catalog.tracks.empty())
    {
        m->error = "No playable music found. Check the root, WAV files and catalog warnings, then reload.";
        return;
    }
    m->director = std::make_unique<EnduranceMusic::Director>(m->catalog.tracks, seed);
    m->audio = std::make_unique<AudioManager>();
    m->appliedVolume = bounded(volume, 0.65f, 0.0f, 1.0f);
    m->audio->setMasterVolume(m->appliedVolume);
}

void EnduranceMusicComponent::sanitizeSettings()
{
    volume = bounded(volume, 0.65f, 0.0f, 1.0f);
    ambienceVolume = bounded(ambienceVolume, 0.55f, 0.0f, 1.0f);
    ambienceDetailVolume = bounded(ambienceDetailVolume, 0.45f, 0.0f, 1.0f);
    weatherVolume = bounded(weatherVolume, 0.85f, 0.0f, 1.0f);
    ambienceTransitionSeconds = bounded(ambienceTransitionSeconds, 4.0f, 0.05f, 30.0f);
    biomeHoldSeconds = bounded(biomeHoldSeconds, 12.0f, 0.0f, 120.0f);
    minimumPlaySeconds = bounded(minimumPlaySeconds, 30.0f, 0.0f, 600.0f);
    fadeInSeconds = bounded(fadeInSeconds, 4.0f, 0.0f, 30.0f);
    fadeOutSeconds = bounded(fadeOutSeconds, 4.0f, 0.0f, 30.0f);
    silenceMinSeconds = bounded(silenceMinSeconds, 20.0f, 0.0f, 600.0f);
    silenceMaxSeconds = bounded(silenceMaxSeconds, 60.0f, silenceMinSeconds, 600.0f);
    recentCompositions = std::clamp(recentCompositions, 0, 32);
}

void EnduranceMusicComponent::update()
{
    if (!m->running) return;
    if (m->pc->soundManager->isPaused()) return;
    sanitizeSettings();
    // Share the sky's sample and validity check with both sound systems. Ambient
    // beds keep playing through music silences, events and an empty music catalog.
    m->sampledBiome = -1;
    const auto sample = EnduranceAtmosphereComponent::currentBiomeSample();
    auto* camera = m->pc->cameraManager ? m->pc->cameraManager->getCurrentCamera() : nullptr;
    if (sample.valid && camera && sample.biome < ProceduralTerrain::BiomeNames.size())
    {
        const glm::vec3 position = camera->getPosition();
        const float dx = position.x - sample.worldXZ.x, dz = position.z - sample.worldXZ.y;
        if (finiteFloat(dx) && finiteFloat(dz) && std::abs(dx) <= 128.0f &&
            std::abs(dz) <= 128.0f && dx * dx + dz * dz <= 128.0f * 128.0f)
            m->sampledBiome = static_cast<int>(sample.biome);
    }
    const float dt = bounded(m->pc->time->getDeltaTime(), 0.0f, 0.0f, 0.1f);
    if (m->ambience)
    {
        if (!ambienceEnabled) m->ambience->stop();
        else
        {
            EnduranceAmbience::Weather weather;
            if (const auto* effects = EnduranceBiomeEffectsComponent::active())
            {
                using EnduranceEnvironment::EffectId;
                const auto strength = [&](EffectId id) { return effects->effectStrength(id); };
                weather.rain = (std::max)({strength(EffectId::Rain), strength(EffectId::HeavyRain),
                    strength(EffectId::Sleet), strength(EffectId::Hail)});
                weather.thunder = strength(EffectId::Thunderstorm);
                weather.sandstorm = strength(EffectId::Sandstorm);
                weather.blizzard = strength(EffectId::Blizzard);
                weather.ashStorm = strength(EffectId::AshStorm);
                weather.lunar = (std::max)(strength(EffectId::LunarDustFall), strength(EffectId::MeteorShower));
            }
            EnduranceAmbience::Settings settings;
            settings.volume = ambienceVolume;
            settings.detailVolume = ambienceDetailVolume;
            settings.weatherVolume = weatherVolume;
            settings.biomeHoldSeconds = biomeHoldSeconds;
            settings.transitionSeconds = ambienceTransitionSeconds;
            m->ambience->update(dt, m->sampledBiome, weather, settings);
        }
    }
    if (!m->audio || !m->director) return;
    if (!enabled)
    {
        if (m->wasEnabled) m->apply(m->director->stop());
        m->wasEnabled = false;
        return;
    }
    m->wasEnabled = true;
    EnduranceMusic::Settings settings;
    settings.biomeHoldSeconds = biomeHoldSeconds;
    settings.minPlaySeconds = minimumPlaySeconds;
    settings.fadeInSeconds = fadeInSeconds;
    settings.fadeOutSeconds = fadeOutSeconds;
    settings.gapMinSeconds = silenceMinSeconds;
    settings.gapMaxSeconds = silenceMaxSeconds;
    settings.recentGroups = static_cast<std::size_t>(recentCompositions);
    m->director->configure(settings);
    if (volume != m->appliedVolume)
    {
        m->appliedVolume = volume;
        m->audio->setMasterVolume(volume);
    }
    m->audio->update();

    const auto status = m->audio->playbackStatus(AudioKey);
    m->apply(m->director->update(dt, m->sampledBiome, playbackInfo(status)));
}

bool EnduranceMusicComponent::requestDiscovery(const std::string& key, int priority,
                                               bool once, float cooldown)
{
    return enabled && m->director && m->director->request(
        EnduranceMusic::Role::Discovery, key, priority, cooldown, once);
}

bool EnduranceMusicComponent::requestLunarEvent(const std::string& key, int priority,
                                              bool once, float cooldown)
{
    return enabled && m->director && m->director->request(
        EnduranceMusic::Role::Lunar, key, priority, cooldown, once);
}

void EnduranceMusicComponent::onGUI()
{
    ImGui::TextUnformatted("ENDURANCE biome music");
    ImGui::Checkbox("Enabled##biome_music", &enabled);
    editPath("Music root", musicRoot);
    editPath("Catalog overrides", catalogFile);
    ImGui::TextWrapped("Paths are read at scene start or reload. Expected albums: Elderweald, Otherworlds, Ancient Horrors.");
    ImGui::SliderFloat("Music gain", &volume, 0.0f, 1.0f);
    ImGui::DragFloat("Biome stability (seconds)", &biomeHoldSeconds, 0.25f, 0.0f, 120.0f);
    ImGui::DragFloat("Minimum playback (seconds)", &minimumPlaySeconds, 0.5f, 0.0f, 600.0f);
    ImGui::DragFloat("Fade in (seconds)", &fadeInSeconds, 0.1f, 0.0f, 30.0f);
    ImGui::DragFloat("Fade out (seconds)", &fadeOutSeconds, 0.1f, 0.0f, 30.0f);
    ImGui::DragFloat("Silence minimum (seconds)", &silenceMinSeconds, 0.5f, 0.0f, 600.0f);
    ImGui::DragFloat("Silence maximum (seconds)", &silenceMaxSeconds, 0.5f, 0.0f, 600.0f);
    ImGui::SliderInt("Recent compositions", &recentCompositions, 0, 32);
    sanitizeSettings();
    if (!m->error.empty()) ImGui::TextWrapped("%s", m->error.c_str());
    if (!m->running) { ImGui::TextUnformatted("Start the scene to load and preview music."); return; }
    if (ImGui::Button("Reload music catalog")) reloadCatalog();
    ImGui::Text("Playable tracks: %u", static_cast<unsigned>(m->catalog.tracks.size()));
    if (!m->catalog.warnings.empty() && ImGui::TreeNode("Music catalog warnings"))
    {
        for (const auto& warning : m->catalog.warnings) ImGui::TextWrapped("%s", warning.c_str());
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Catalog families and roles"))
    {
        constexpr EnduranceMusic::Family families[] = {
            EnduranceMusic::Cold, EnduranceMusic::Verdant, EnduranceMusic::Vast,
            EnduranceMusic::Mystery, EnduranceMusic::Ocean, EnduranceMusic::Lunar};
        for (std::size_t i = 0; i < m->catalog.tracks.size(); ++i)
        {
            const auto& track = m->catalog.tracks[i];
            std::string names;
            for (const auto family : families)
                if (track.families & family)
                {
                    if (!names.empty()) names += ", ";
                    names += EnduranceMusic::familyName(family);
                }
            ImGui::TextWrapped("%s", track.id.c_str());
            ImGui::TextWrapped("%s / %s / gain %.2f%s", EnduranceMusic::roleName(track.role),
                names.c_str(), track.gain, m->director && m->director->unavailable(i) ? " (load failed)" : "");
        }
        ImGui::TreePop();
    }
    if (!m->director) return;
    const int biome = m->director->stableBiome();
    if (biome >= 0 && static_cast<std::size_t>(biome) < ProceduralTerrain::BiomeNames.size())
        ImGui::Text("Stable biome: %s", ProceduralTerrain::BiomeNames[biome]);
    else ImGui::TextUnformatted("Waiting for a stable biome sample.");
    const auto index = m->director->currentTrack();
    if (index < m->catalog.tracks.size())
    {
        const auto& track = m->catalog.tracks[index];
        const auto status = m->audio->playbackStatus(AudioKey);
        ImGui::TextWrapped("Current: %s", track.id.c_str());
        ImGui::TextWrapped("Composition: %s", track.group.c_str());
        ImGui::Text("Remaining: %.1fs%s", status.remainingSeconds,
            status.state == AudioPlaybackState::LOADING ? " (loading)" : "");
    }
    else ImGui::TextUnformatted("Music is resting or waiting for an eligible track.");
    ImGui::Text("Pending narrative cue: %s", m->director->hasPendingEvent() ? "yes" : "no");
    if (ImGui::Button("Preview discovery"))
        m->error = requestDiscovery("editor/discovery", 50, false, 0.0f) ? "" : "No discovery cue available, or another event has priority.";
    ImGui::SameLine();
    if (ImGui::Button("Preview lunar event"))
        m->error = requestLunarEvent("editor/lunar", 100, false, 0.0f) ? "" : "No lunar cue available, or another event has priority.";
    ImGui::TextWrapped("Ancient Horrors only plays on explicit lunar events. Reload clears session history and event cooldowns.");
}

std::string EnduranceMusicComponent::serialize() { return JS::serializeStruct(*this); }
void EnduranceMusicComponent::load(std::string data)
{
    JS::ParseContext context(data);
    if (context.parseTo(*this) != JS::Error::NoError) m->error = "Invalid music scene settings.";
    sanitizeSettings();
}
