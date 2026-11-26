#pragma once

#include "AudioManager.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace EnduranceAmbience
{
enum class Clip : std::uint8_t
{
    Coast, TreeCreaks, Forest, GalacticWind, IceCrack, Lava, Rain, Stream,
    SnowGusts, Crickets, ThunderClap, ThunderStrike, VolcanicRumble,
    BlusteryWind, DesertWind, MountainWind, DestructiveWind, Count, None = Count
};
inline constexpr std::size_t ClipCount = static_cast<std::size_t>(Clip::Count);
struct Recording
{
    const char* file;
    // Optional, baked once on the audio worker for recordings not sold as loops.
    // Never changes the original WAV, the music, or one-shot playback.
    float loopBlendSeconds = 0.0f;
};
struct Profile
{
    Clip bed = Clip::None;
    float bedGain = 0.0f;
    Clip detail = Clip::None;
    float detailGain = 0.0f;
    float gapMin = 40.0f, gapMax = 100.0f;
};
const std::array<Recording, ClipCount>& recordings();
const std::array<Profile, ProceduralTerrain::BiomeCount>& biomeProfiles();

// Values only: no ImGui dependency and no added rendering/climate work.
struct Settings
{
    float volume = 0.55f;
    float detailVolume = 0.45f;
    float weatherVolume = 0.85f;
    float biomeHoldSeconds = 12.0f;
    float transitionSeconds = 4.0f;
};
// Snapshot of the existing visual weather events, not a second weather clock.
struct Weather
{
    float rain = 0.0f, thunder = 0.0f, sandstorm = 0.0f;
    float blizzard = 0.0f, ashStorm = 0.0f, lunar = 0.0f;
};

// Owns one asynchronous AudioManager. Two bed voices + two weather voices for
// crossfades, and at most one detail/thunder voice; no unbounded buffer cache.
// Main-thread API. Weather and ambience use SFX, never musicStopper().
class Player
{
public:
    explicit Player(const std::string& root, std::uint32_t seed = 0xAB1E1234u);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    void update(float dt, int sampledBiome, const Weather& weather, const Settings& settings);
    void stop();
    int stableBiome() const { return m_biome; }
    bool unavailable(Clip clip) const;
    static constexpr std::size_t MaxVoices = 5;

private:
    struct Voice
    {
        Clip clip = Clip::None;
        float gain = -1.0f, loadingAge = 0.0f;
        bool started = false, retiring = false;
    };
    AudioManager m_audio;
    const ptrClass* m_pc;
    std::array<std::string, ClipCount> m_paths;
    std::array<bool, ClipCount> m_failed{};
    std::array<Voice, MaxVoices> m_voices{};
    std::mt19937 m_random;
    int m_biome = -1, m_candidate = -1;
    float m_candidateAge = 0.0f, m_invalidAge = 0.0f;
    float m_volume = -1.0f, m_cueWait = 0.0f;
    Clip m_cueGroup = Clip::None, m_lastThunder = Clip::None;

    float randomGap(float minimum, float maximum);
    void release(std::size_t slot);
    void fadeOut(std::size_t slot, float seconds);
    void gain(std::size_t slot, float value, float seconds);
    void start(std::size_t slot, Clip clip, bool loop);
    void poll(float dt);
    void loopLane(std::size_t first, Clip target, float level, float fade);
    void cue(float dt, Clip clip, float level, float gapMin, float gapMax);
};
}
