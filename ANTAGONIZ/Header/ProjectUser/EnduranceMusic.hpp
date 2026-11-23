#pragma once

#include "Behaviour.hpp"
#include <memory>
#include <string>

// Game policy only: playback, loading and the global music bus remain owned by
// AudioManager / SoundManager. This behaviour does not submit rendering work.
class EnduranceMusicComponent final : public Behaviour
{
public:
    EnduranceMusicComponent();
    ~EnduranceMusicComponent() override;
    void start() override;
    void stop() override;
    void update() override;
    void fixedUpdate() override {}
    void onGUI() override;
    std::string serialize() override;
    void load(std::string data) override;

    static EnduranceMusicComponent* active();
    // Call from gameplay on the main thread. A stable event key prevents a
    // discovery from restarting each frame. "once" lasts for this scene session.
    bool requestDiscovery(const std::string& eventKey, int priority = 50,
                          bool once = true, float cooldownSeconds = 180.0f);
    bool requestLunarEvent(const std::string& eventKey, int priority = 100,
                          bool once = true, float cooldownSeconds = 180.0f);
    void reloadCatalog();

    bool enabled = true;
    std::string musicRoot = "../ENDURANCE/Ressources/Sound";
    std::string catalogFile = "../ENDURANCE/Ressources/Sound/music_catalog.json";
    float volume = 0.65f;
    float biomeHoldSeconds = 12.0f;
    float minimumPlaySeconds = 30.0f;
    float fadeInSeconds = 4.0f, fadeOutSeconds = 4.0f;
    float silenceMinSeconds = 20.0f, silenceMaxSeconds = 60.0f;
    int recentCompositions = 5;

private:
    void sanitizeSettings();
    struct Impl;
    std::unique_ptr<Impl> m;
};
