#pragma once

#include "EnduranceMusicCatalog.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace EnduranceMusic
{
enum class Playback { Stopped, Loading, Playing, Paused };
struct PlaybackInfo
{
    Playback state = Playback::Stopped;
    // Zero means unknown. The backend, rather than an estimated track length,
    // remains authoritative about the end of a piece.
    float remainingSeconds = 0.0f;
};

struct Settings
{
    float biomeHoldSeconds = 12.0f;
    float minPlaySeconds = 30.0f;
    float fadeInSeconds = 4.0f;
    float fadeOutSeconds = 4.0f;
    float gapMinSeconds = 20.0f;
    float gapMaxSeconds = 60.0f;
    float loadTimeoutSeconds = 30.0f;
    float retryDelaySeconds = 5.0f;
    std::size_t recentGroups = 5;
};

inline constexpr std::size_t NoTrack = static_cast<std::size_t>(-1);
enum class CommandType { Play, FadeOut, Release };
struct Command
{
    CommandType type = CommandType::Release;
    std::size_t track = NoTrack;
    // Play: fade-in duration; FadeOut: fade-out duration.
    float seconds = 0.0f;
};
struct Actions
{
    // At most release the old sound and start the next. No allocations in idle
    // frames and no simultaneous compositions, even when an event interrupts.
    std::array<Command, 2> commands{};
    std::size_t count = 0;
};

// Pure game policy. The catalog must outlive the director and remain unchanged.
// Call on the same thread as the owning audio component; no GPU/audio APIs here.
class Director
{
public:
    explicit Director(const std::vector<Track>& tracks, std::uint32_t seed = 0xED012345u);
    void configure(const Settings& settings);
    const Settings& settings() const { return m_settings; }

    // biome < 0 (or outside the catalog's mapping) means unavailable/stale.
    // playback describes currentTrack(), including the async Loading stage.
    Actions update(float deltaSeconds, int biome, PlaybackInfo playback);

    // Only Discovery/Lunar requests are accepted. At most one pending request;
    // a higher priority can replace it and interrupt a lower-priority piece.
    // Cooldown/once are consumed only after the backend reports Playing.
    bool request(Role role, const std::string& eventKey, int priority,
                 float cooldownSeconds = 180.0f, bool once = false,
                 float ttlSeconds = 120.0f);

    // stop preserves history, cooldowns and failed-track exclusions. reset is
    // for a fresh session and requires the caller to release any old sound first.
    Actions stop();
    void reset();
    std::size_t currentTrack() const { return m_current; }
    int stableBiome() const { return m_stableBiome; }
    bool hasPendingEvent() const { return m_pending.has_value(); }
    bool unavailable(std::size_t index) const;

private:
    enum class Phase { Idle, Starting, Playing, Fading };
    struct Event
    {
        Role role = Role::Discovery;
        std::string key;
        int priority = 0;
        float cooldown = 180.0f;
        bool once = false;
        double expires = 0.0;
    };
    struct EventMemory { double nextAllowed = 0.0; bool onceConsumed = false; };

    const std::vector<Track>& m_tracks;
    Settings m_settings;
    std::mt19937 m_random;
    std::vector<bool> m_unavailable;
    std::vector<std::string> m_recent;
    std::unordered_map<std::string, std::uint64_t> m_lastGroupPlay;
    std::unordered_map<std::string, EventMemory> m_events;
    std::optional<Event> m_pending, m_activeEvent;
    Phase m_phase = Phase::Idle;
    std::size_t m_current = NoTrack, m_lastTrack = NoTrack;
    std::uint64_t m_playSerial = 0;
    double m_now = 0.0, m_nextStart = 0.0, m_retryAt = 0.0;
    double m_candidateAge = 0.0, m_played = 0.0, m_loadingAge = 0.0, m_fadingAge = 0.0;
    float m_fadeDuration = 0.0f;
    int m_candidateBiome = -1, m_stableBiome = -1;

    std::string group(std::size_t index) const;
    bool hasEligible(Role role) const;
    std::size_t choose(Role role, FamilyMask families);
    void started();
    void release(Actions& out, bool failed);
    void beginFade(Actions& out, float seconds);
    void tryStart(Actions& out, bool validBiome);
};
}
