#include "EnduranceAmbience.hpp"
#include "Engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace EnduranceAmbience
{
namespace
{
    constexpr std::array<const char*, Player::MaxVoices> Keys{{
        "endurance/ambience/bed-a", "endurance/ambience/bed-b",
        "endurance/ambience/weather-a", "endurance/ambience/weather-b",
        "endurance/ambience/detail"}};
    float bounded(float value, float fallback, float lo, float hi)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits)); // Also valid with -ffast-math.
        return (bits & 0x7f800000u) == 0x7f800000u ? fallback : std::clamp(value, lo, hi);
    }
    float strength(float value) { return bounded(value, 0.0f, 0.0f, 1.0f); }
    constexpr std::size_t index(Clip clip) { return static_cast<std::size_t>(clip); }
}

const std::array<Recording, ClipCount>& recordings()
{
    static const std::array<Recording, ClipCount> data{{
        {"Coast 2014 Coast Mono 1.wav", 1.0f},
        {"Creaks and Snaps 2017.03.20 - Gentle Tree Creaks.WAV"},
        {"FRST - Amb Forest 2.wav", 1.0f},
        {"Galactic Wind.wav", 1.0f},
        {"Ice,Block,Crack,Distant,Medium_03.wav"},
        {"Lava Lava Windy.WAV", 1.0f},
        {"Rain 2016 Steens Rain No Pops Short 4.WAV", 0.75f},
        {"Streams quietstream.wav", 1.0f},
        {"Wind Gusts 2016 Lookout Snow - Biggest Gusts Long.wav", 1.0f},
        {"forest_alpine_crickets_01_60_loop.wav"},
        {"thunder_clap_04.wav"},
        {"thunder_strike_03.wav"},
        {"volcanique hell ambiance 01.wav", 1.0f},
        {"wind_blustery_08_loop.wav"},
        {"wind_desert_01_60_loop.wav"},
        {"wind_harsh_mountain_02_30_loop.wav"},
        {"wind_tornado_with_destruction_01.wav", 1.0f}
    }};
    return data;
}

const std::array<Profile, ProceduralTerrain::BiomeCount>& biomeProfiles()
{
    // Same stable ID order as terrain, music and atmosphere. Stream recordings
    // are quiet biome accents, not spatial river emitters (no water query here).
    static const std::array<Profile, ProceduralTerrain::BiomeCount> profiles{{
        {Clip::MountainWind, .75f, Clip::IceCrack, .65f, 35, 90},   // PolarIce
        {Clip::BlusteryWind, .65f, Clip::IceCrack, .45f, 50, 110},  // FrozenTundra
        {Clip::BlusteryWind, .50f},                               // Tundra
        {Clip::Forest, .60f, Clip::TreeCreaks, .55f},              // Taiga
        {Clip::Crickets, .50f, Clip::Stream, .22f},                // BorealBog
        {Clip::MountainWind, .60f, Clip::IceCrack, .30f, 65, 140},  // RockyTundra
        {Clip::BlusteryWind, .55f},                               // ColdSteppe
        {Clip::Forest, .75f, Clip::TreeCreaks, .55f},              // BorealForest
        {Clip::Forest, .60f, Clip::Stream, .20f},                  // ColdForest
        {Clip::Crickets, .45f, Clip::Stream, .25f},                // ColdMarsh
        {Clip::BlusteryWind, .50f},                               // DryPlain
        {Clip::Crickets, .60f},                                  // Meadow
        {Clip::Forest, .90f, Clip::TreeCreaks, .50f},              // TemperateForest
        {Clip::Forest, .85f, Clip::Stream, .30f},                  // TemperateRainforest
        {Clip::Crickets, .65f, Clip::Stream, .30f},                // TemperateMarsh
        {Clip::DesertWind, .60f},                                 // RockDesert
        {Clip::BlusteryWind, .45f, Clip::Crickets, .30f},          // DrySavanna
        {Clip::Crickets, .65f},                                  // Savanna
        {Clip::Forest, .60f, Clip::TreeCreaks, .35f},              // DryJungle
        {Clip::Forest, .90f, Clip::Crickets, .35f},                // TropicalJungle
        {Clip::Lava, .60f, Clip::VolcanicRumble, .40f},            // VolcanicDesert
        {Clip::DesertWind, .65f},                                 // RedDesert
        {Clip::MountainWind, .55f},                               // AridPlateau
        {Clip::Lava, .45f, Clip::VolcanicRumble, .25f},            // BurningJungle
        {Clip::Coast, .65f, Clip::Crickets, .25f},                 // TropicalMangrove
        {Clip::Coast, .80f}                                      // Marine
    }};
    return profiles;
}

Player::Player(const std::string& root, std::uint32_t seed)
    : m_pc(Engine::getPtrClassAddr()), m_random(seed)
{
    const std::string prefix = root + (root.empty() || root.back() == '/' || root.back() == '\\' ? "" : "/");
    for (std::size_t i = 0; i < ClipCount; ++i) m_paths[i] = prefix + recordings()[i].file;
}
Player::~Player() { stop(); }
bool Player::unavailable(Clip clip) const { return index(clip) >= ClipCount || m_failed[index(clip)]; }
float Player::randomGap(float minimum, float maximum)
{ return std::uniform_real_distribution<float>(minimum, maximum)(m_random); }

void Player::release(std::size_t slot)
{
    if (m_voices[slot].clip != Clip::None) m_audio.releaseSound(Keys[slot]);
    m_voices[slot] = {};
}
void Player::fadeOut(std::size_t slot, float seconds)
{
    auto& voice = m_voices[slot];
    if (voice.clip == Clip::None || voice.retiring) return;
    if (!voice.started) { release(slot); return; }
    m_audio.stopFade(Keys[slot], seconds);
    voice.retiring = true;
    voice.gain = 0.0f;
}
void Player::gain(std::size_t slot, float value, float seconds)
{
    auto& voice = m_voices[slot];
    if (voice.retiring || std::abs(voice.gain - value) >= .005f)
    {
        m_audio.setVolume(Keys[slot], value, seconds);
        voice.gain = value;
        voice.retiring = false;
    }
}
void Player::start(std::size_t slot, Clip clip, bool loop)
{
    m_voices[slot] = {};
    m_voices[slot].clip = clip;
    m_audio.registerSound(Keys[slot], {m_paths[index(clip)]});
    m_audio.play(Keys[slot], loop ? AudioPlayType::LOOP : AudioPlayType::ONCE,
        SoundType::SFX, 0, 0.0f, loop ? recordings()[index(clip)].loopBlendSeconds : 0.0f);
}
void Player::poll(float dt)
{
    for (std::size_t i = 0; i < MaxVoices; ++i)
    {
        auto& voice = m_voices[i];
        if (voice.clip == Clip::None) continue;
        const auto status = m_audio.playbackStatus(Keys[i]);
        if (!voice.started)
        {
            voice.loadingAge += dt;
            if (status.state == AudioPlaybackState::PLAYING) voice.started = true;
            else if (status.state == AudioPlaybackState::STOPPED || voice.loadingAge >= 30.0f)
            {
                m_failed[index(voice.clip)] = true; // Once per scene, not every frame.
                release(i);
            }
        }
        else if (status.state == AudioPlaybackState::STOPPED) release(i);
    }
}

void Player::loopLane(std::size_t first, Clip target, float level, float fade)
{
    if (unavailable(target) || level <= .001f) target = Clip::None;
    // Discard an obsolete decode immediately. The load token prevents it from
    // becoming audible later, even if the file was already being decoded.
    for (std::size_t i = first; i < first + 2; ++i)
        if (m_voices[i].clip != target && (!m_voices[i].started || m_voices[i].gain < 0.0f)) release(i);
    if (target == Clip::None)
    {
        fadeOut(first, fade); fadeOut(first + 1, fade);
        return;
    }
    for (std::size_t i = first; i < first + 2; ++i)
        if (m_voices[i].clip == target)
        {
            if (m_voices[i].started)
            {
                gain(i, level, fade); // Reversing a fade keeps the current audible gain.
                fadeOut(first + (i == first ? 1 : 0), fade);
            }
            return; // Keep the old ambience until the new one is actually ready.
        }
    for (std::size_t i = first; i < first + 2; ++i)
        if (m_voices[i].clip == Clip::None) { start(i, target, true); return; }
    // A third target waits for the outgoing voice; never grows the voice pool.
}

void Player::cue(float dt, Clip clip, float level, float gapMin, float gapMax)
{
    constexpr std::size_t slot = 4;
    auto& voice = m_voices[slot];
    if ((unavailable(clip) && (clip != Clip::ThunderClap || unavailable(Clip::ThunderStrike))) ||
        level <= .001f) clip = Clip::None;
    if (clip != m_cueGroup)
    {
        m_cueGroup = clip;
        m_cueWait = randomGap(gapMin, gapMax);
        fadeOut(slot, 1.5f);
    }
    if (clip == Clip::None) { fadeOut(slot, 1.5f); return; }
    if (voice.clip != Clip::None)
    {
        if (!voice.retiring && voice.started)
        {
            const float remaining = m_audio.playbackStatus(Keys[slot]).remainingSeconds;
            if (remaining > 0.0f && remaining <= 0.75f) fadeOut(slot, remaining);
            else gain(slot, level, .2f);
        }
        return;
    }
    m_cueWait -= dt;
    if (m_cueWait > 0.0f) return;
    Clip selected = clip;
    if (clip == Clip::ThunderClap)
    {
        selected = m_lastThunder == Clip::ThunderClap ? Clip::ThunderStrike : Clip::ThunderClap;
        if (unavailable(selected)) selected = unavailable(clip) ? Clip::ThunderStrike : clip;
        m_lastThunder = selected;
    }
    start(slot, selected, false);
    m_cueWait = randomGap(gapMin, gapMax); // Gap measured after completion, not after loading.
}

void Player::update(float seconds, int biome, const Weather& weather, const Settings& settings)
{
    if (m_pc && m_pc->soundManager && m_pc->soundManager->isPaused()) return;
    const float dt = bounded(seconds, 0.0f, 0.0f, .1f);
    const float volume = bounded(settings.volume, .55f, 0.0f, 1.0f);
    if (volume != m_volume) { m_volume = volume; m_audio.setMasterVolume(volume); }
    const float fade = bounded(settings.transitionSeconds, 4.0f, .05f, 30.0f);
    const float hold = bounded(settings.biomeHoldSeconds, 12.0f, 0.0f, 120.0f);
    const bool valid = biome >= 0 && static_cast<std::size_t>(biome) < biomeProfiles().size();
    if (valid)
    {
        m_invalidAge = 0.0f;
        if (m_candidate != biome) { m_candidate = biome; m_candidateAge = 0.0f; }
        m_candidateAge += dt;
        if (m_candidateAge >= hold) m_biome = biome;
    }
    else
    {
        m_candidate = -1; m_candidateAge = 0.0f; m_invalidAge += dt;
        if (m_invalidAge >= 2.0f) m_biome = -1;
    }
    m_audio.update();
    poll(dt);
    Profile profile;
    if (m_biome >= 0) profile = biomeProfiles()[static_cast<std::size_t>(m_biome)];
    Clip bed = profile.bed;
    if (bed != Clip::None && unavailable(bed)) bed = Clip::BlusteryWind;
    loopLane(0, bed, profile.bedGain, fade);

    Clip event = Clip::None;
    float eventGain = 0.0f;
    const auto select = [&](Clip candidate, float value) {
        const float amount = strength(value);
        if (amount > eventGain && !unavailable(candidate)) { event = candidate; eventGain = amount; }
    };
    if (valid)
    {
        select(Clip::Rain, (std::max)(weather.rain, weather.thunder));
        select(Clip::SnowGusts, weather.blizzard);
        select(Clip::DestructiveWind, (std::max)(weather.sandstorm, weather.ashStorm));
        select(Clip::GalacticWind, weather.lunar);
    }
    const float weatherVolume = bounded(settings.weatherVolume, .85f, 0.0f, 1.0f);
    loopLane(2, event, eventGain * weatherVolume, fade);
    const bool thunder = valid && strength(weather.thunder) > .05f;
    const float details = bounded(settings.detailVolume, .45f, 0.0f, 1.0f);
    cue(dt, thunder ? Clip::ThunderClap : valid ? profile.detail : Clip::None,
        thunder ? strength(weather.thunder) * weatherVolume * .8f : profile.detailGain * details,
        thunder ? 12.0f : profile.gapMin, thunder ? 35.0f : profile.gapMax);
}

void Player::stop()
{
    for (std::size_t i = 0; i < MaxVoices; ++i) release(i);
    m_biome = m_candidate = -1;
    m_candidateAge = m_invalidAge = m_cueWait = 0.0f;
    m_cueGroup = Clip::None;
}
}
