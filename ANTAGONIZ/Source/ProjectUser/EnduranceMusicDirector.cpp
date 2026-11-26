#include "EnduranceMusicDirector.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace EnduranceMusic
{
namespace
{
bool finite(float value)
{
    // The game is also built with fast-math; std::isfinite can be optimized
    // away there. Do not let a bad editor value poison all scheduling clocks.
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "32-bit floats required");
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

float bounded(float value, float fallback, float low, float high)
{
    return finite(value) ? (std::max)(low, (std::min)(value, high)) : fallback;
}

unsigned bitCount(FamilyMask mask)
{
    unsigned result = 0;
    while (mask != 0) { mask &= mask - 1; ++result; }
    return result;
}
}

Director::Director(const std::vector<Track>& tracks, std::uint32_t seed)
    : m_tracks(tracks), m_random(seed), m_unavailable(tracks.size(), false)
{
}

void Director::configure(const Settings& settings)
{
    const Settings defaults;
    m_settings.biomeHoldSeconds = bounded(settings.biomeHoldSeconds, defaults.biomeHoldSeconds, 0.0f, 600.0f);
    m_settings.minPlaySeconds = bounded(settings.minPlaySeconds, defaults.minPlaySeconds, 0.0f, 3600.0f);
    m_settings.fadeInSeconds = bounded(settings.fadeInSeconds, defaults.fadeInSeconds, 0.0f, 30.0f);
    m_settings.fadeOutSeconds = bounded(settings.fadeOutSeconds, defaults.fadeOutSeconds, 0.0f, 30.0f);
    m_settings.gapMinSeconds = bounded(settings.gapMinSeconds, defaults.gapMinSeconds, 0.0f, 3600.0f);
    m_settings.gapMaxSeconds = bounded(settings.gapMaxSeconds, defaults.gapMaxSeconds, 0.0f, 3600.0f);
    if (m_settings.gapMaxSeconds < m_settings.gapMinSeconds)
        std::swap(m_settings.gapMaxSeconds, m_settings.gapMinSeconds);
    m_settings.loadTimeoutSeconds = bounded(settings.loadTimeoutSeconds, defaults.loadTimeoutSeconds, 1.0f, 300.0f);
    m_settings.retryDelaySeconds = bounded(settings.retryDelaySeconds, defaults.retryDelaySeconds, 0.5f, 300.0f);
    m_settings.recentGroups = (std::min)(settings.recentGroups, std::size_t{64});
    if (m_recent.size() > m_settings.recentGroups)
        m_recent.erase(m_recent.begin(), m_recent.end() - m_settings.recentGroups);
}

bool Director::unavailable(std::size_t index) const
{
    return index >= m_unavailable.size() || m_unavailable[index];
}

std::string Director::group(std::size_t index) const
{
    const Track& track = m_tracks[index];
    if (!track.group.empty()) return track.group;
    return track.id.empty() ? track.path : track.id;
}

bool Director::hasEligible(Role role) const
{
    for (std::size_t i = 0; i < m_tracks.size(); ++i)
        if (!unavailable(i) && m_tracks[i].role == role) return true;
    return false;
}

bool Director::request(Role role, const std::string& eventKey, int priority,
                       float cooldownSeconds, bool once, float ttlSeconds)
{
    const bool supported = hasEligible(role)
        || (role == Role::Discovery && hasEligible(Role::Exploration));
    if (role == Role::Exploration || eventKey.empty() || !supported) return false;
    const auto memory = m_events.find(eventKey);
    if (memory != m_events.end()
        && (memory->second.onceConsumed || m_now < memory->second.nextAllowed)) return false;
    if (m_activeEvent && m_activeEvent->key == eventKey) return false;
    if (m_pending && m_pending->expires <= m_now) m_pending.reset();
    // Repeated per-frame triggers cannot refresh the same event forever.
    if (m_pending && (m_pending->key == eventKey || m_pending->priority >= priority)) return false;
    Event event;
    event.role = role;
    event.key = eventKey;
    event.priority = priority;
    event.cooldown = bounded(cooldownSeconds, 180.0f, 0.0f, 86400.0f);
    event.once = once;
    event.expires = m_now + bounded(ttlSeconds, 120.0f, 0.1f, 3600.0f);
    m_pending = std::move(event);
    return true;
}

std::size_t Director::choose(Role role, FamilyMask families)
{
    struct Group
    {
        std::string name;
        unsigned weight = 1;
        std::uint64_t last = 0;
        bool recent = false;
        std::vector<std::size_t> tracks;
    };
    std::vector<Group> groups;
    for (std::size_t i = 0; i < m_tracks.size(); ++i)
    {
        const Track& track = m_tracks[i];
        if (unavailable(i) || track.role != role) continue;
        const unsigned weight = role == Role::Exploration ? bitCount(track.families & families) : 1;
        if (weight == 0) continue;
        const std::string name = group(i);
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.name == name; });
        if (found == groups.end())
        {
            Group candidate;
            candidate.name = name;
            candidate.weight = weight;
            const auto last = m_lastGroupPlay.find(name);
            candidate.last = last == m_lastGroupPlay.end() ? 0 : last->second;
            candidate.recent = std::find(m_recent.begin(), m_recent.end(), name) != m_recent.end();
            candidate.tracks.push_back(i);
            groups.push_back(std::move(candidate));
        }
        else
        {
            // Ten file variations must not make a composition ten times more
            // probable than a composition shipped as one file.
            found->weight = (std::max)(found->weight, weight);
            found->tracks.push_back(i);
        }
    }
    if (groups.empty()) return NoTrack;
    const bool hasFresh = std::any_of(groups.begin(), groups.end(), [](const Group& g) { return !g.recent; });
    std::uint64_t oldest = (std::numeric_limits<std::uint64_t>::max)();
    if (!hasFresh)
        for (const Group& g : groups) oldest = (std::min)(oldest, g.last);

    const std::string lastGroup = m_lastTrack == NoTrack ? std::string{} : group(m_lastTrack);
    unsigned totalWeight = 0;
    for (const Group& g : groups)
    {
        const bool eligible = hasFresh ? !g.recent : g.last == oldest;
        if (eligible && (groups.size() == 1 || g.name != lastGroup)) totalWeight += g.weight;
    }
    // With history disabled, explicitly exclude the immediately previous
    // composition whenever another compatible composition is available.
    if (totalWeight == 0) return NoTrack;
    std::uniform_int_distribution<unsigned> pick(1, totalWeight);
    unsigned ticket = pick(m_random);
    for (const Group& g : groups)
    {
        const bool eligible = hasFresh ? !g.recent : g.last == oldest;
        if (!eligible || (groups.size() > 1 && g.name == lastGroup)) continue;
        if (ticket > g.weight) { ticket -= g.weight; continue; }
        std::vector<std::size_t> variants;
        for (std::size_t index : g.tracks)
            if (g.tracks.size() == 1 || index != m_lastTrack) variants.push_back(index);
        std::uniform_int_distribution<std::size_t> variant(0, variants.size() - 1);
        return variants[variant(m_random)];
    }
    return NoTrack;
}

void Director::started()
{
    m_phase = Phase::Playing;
    m_played = 0.0;
    m_lastTrack = m_current;
    const std::string name = group(m_current);
    m_lastGroupPlay[name] = ++m_playSerial;
    m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), name), m_recent.end());
    m_recent.push_back(name);
    if (m_recent.size() > m_settings.recentGroups) m_recent.erase(m_recent.begin());
    if (m_activeEvent)
    {
        EventMemory& memory = m_events[m_activeEvent->key];
        memory.nextAllowed = m_now + m_activeEvent->cooldown;
        memory.onceConsumed = memory.onceConsumed || m_activeEvent->once;
    }
}

void Director::release(Actions& out, bool failed)
{
    out.commands[out.count++] = {CommandType::Release, m_current, 0.0f};
    if (failed)
    {
        m_unavailable[m_current] = true;
        m_retryAt = m_now + m_settings.retryDelaySeconds;
        // A failed load did not deliver the event. Try a different composition
        // after backoff if the request is still relevant.
        if (m_activeEvent && m_activeEvent->expires > m_now
            && (!m_pending || m_pending->priority < m_activeEvent->priority))
            m_pending = m_activeEvent;
        m_nextStart = m_retryAt;
    }
    else
    {
        std::uniform_real_distribution<float> gap(m_settings.gapMinSeconds, m_settings.gapMaxSeconds);
        m_nextStart = m_now + gap(m_random);
    }
    m_current = NoTrack;
    m_phase = Phase::Idle;
    m_activeEvent.reset();
    m_played = m_loadingAge = m_fadingAge = 0.0;
}

void Director::beginFade(Actions& out, float seconds)
{
    if (seconds <= 0.0f) { release(out, false); return; }
    out.commands[out.count++] = {CommandType::FadeOut, m_current, seconds};
    m_phase = Phase::Fading;
    m_fadingAge = 0.0;
    m_fadeDuration = seconds;
}

void Director::tryStart(Actions& out, bool validBiome)
{
    if (m_phase != Phase::Idle || m_now < m_retryAt) return;
    const bool event = m_pending.has_value();
    if (!event && (!validBiome || m_stableBiome != m_candidateBiome || m_now < m_nextStart)) return;
    Role role = event ? m_pending->role : Role::Exploration;
    FamilyMask families = event ? 0 : biomeFamilies(m_stableBiome);
    if (role == Role::Discovery && !hasEligible(Role::Discovery))
    {
        // Dedicated discovery cues are optional. Keep priority/cooldown event
        // semantics, using a mysterious, expansive or locally fitting piece.
        // Lunar cues deliberately never fall back to exploration music.
        role = Role::Exploration;
        families = Family::Mystery | Family::Vast | biomeFamilies(m_stableBiome);
    }
    const std::size_t index = choose(role, families);
    if (index == NoTrack)
    {
        if (event) m_pending.reset();
        // An empty catalog or unsupported biome should not rescan every frame.
        m_retryAt = m_now + m_settings.retryDelaySeconds;
        return;
    }
    m_current = index;
    m_phase = Phase::Starting;
    m_loadingAge = 0.0;
    if (event) { m_activeEvent = m_pending; m_pending.reset(); }
    out.commands[out.count++] = {CommandType::Play, index, m_settings.fadeInSeconds};
}

Actions Director::update(float deltaSeconds, int biome, PlaybackInfo playback)
{
    Actions out;
    // Pause neither consumes the rest period nor expires a discovery request.
    if (playback.state == Playback::Paused) return out;
    const float dt = bounded(deltaSeconds, 0.0f, 0.0f, 3600.0f);
    m_now += dt;
    if (m_pending && m_pending->expires <= m_now) m_pending.reset();

    const bool validBiome = biomeFamilies(biome) != 0;
    if (!validBiome) { m_candidateBiome = -1; m_candidateAge = 0.0; }
    else
    {
        if (m_candidateBiome != biome) { m_candidateBiome = biome; m_candidateAge = 0.0; }
        m_candidateAge += dt;
        if (m_candidateAge >= m_settings.biomeHoldSeconds) m_stableBiome = biome;
    }

    if (m_phase == Phase::Starting)
    {
        const bool expired = m_activeEvent && m_activeEvent->expires <= m_now;
        const bool superseded = m_pending
            && (!m_activeEvent || m_pending->priority > m_activeEvent->priority);
        if (expired || superseded) release(out, false);
        else if (playback.state == Playback::Playing) started();
        else
        {
            m_loadingAge += dt;
            if (playback.state == Playback::Stopped || m_loadingAge >= m_settings.loadTimeoutSeconds)
                release(out, true);
        }
    }
    else if (m_phase == Phase::Fading)
    {
        m_fadingAge += dt;
        // Backend stop is authoritative. The extra second only guards a failed
        // or removed backend fade so it cannot leave the scheduler stuck.
        if (playback.state == Playback::Stopped || m_fadingAge >= m_fadeDuration + 1.0)
            release(out, false);
    }
    else if (m_phase == Phase::Playing)
    {
        if (playback.state == Playback::Stopped) release(out, false);
        else if (playback.state == Playback::Playing) m_played += dt;
    }

    if (m_phase == Phase::Playing)
    {
        const bool priorityInterrupt = m_pending
            && (!m_activeEvent || m_pending->priority > m_activeEvent->priority);
        const bool incompatible = !m_activeEvent && validBiome && m_stableBiome >= 0
            && (m_tracks[m_current].families & biomeFamilies(m_stableBiome)) == 0
            && m_played >= m_settings.minPlaySeconds;
        const bool nearEnd = finite(playback.remainingSeconds) && playback.remainingSeconds > 0.0f
            && playback.remainingSeconds <= m_settings.fadeOutSeconds;
        if (priorityInterrupt || incompatible || nearEnd)
        {
            const float duration = nearEnd
                ? (std::min)(m_settings.fadeOutSeconds, playback.remainingSeconds) : m_settings.fadeOutSeconds;
            beginFade(out, duration);
        }
    }
    tryStart(out, validBiome);
    return out;
}

Actions Director::stop()
{
    Actions out;
    if (m_current != NoTrack) out.commands[out.count++] = {CommandType::Release, m_current, 0.0f};
    m_current = NoTrack;
    m_phase = Phase::Idle;
    m_pending.reset();
    m_activeEvent.reset();
    m_candidateBiome = m_stableBiome = -1;
    m_candidateAge = m_played = m_loadingAge = m_fadingAge = 0.0;
    m_nextStart = m_now;
    return out;
}

void Director::reset()
{
    stop();
    std::fill(m_unavailable.begin(), m_unavailable.end(), false);
    m_recent.clear();
    m_lastGroupPlay.clear();
    m_events.clear();
    m_now = m_nextStart = m_retryAt = 0.0;
    m_lastTrack = NoTrack;
    m_playSerial = 0;
}
}
