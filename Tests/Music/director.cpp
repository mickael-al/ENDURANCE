#include "EnduranceMusicDirector.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

using namespace EnduranceMusic;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)

Track track(const char* id, FamilyMask families, Role role = Role::Exploration, const char* group = "")
{
    return {id, std::string(id) + ".wav", *group ? group : id, families, role, 1.0f};
}

Settings immediate()
{
    Settings settings;
    settings.biomeHoldSeconds = settings.gapMinSeconds = settings.gapMaxSeconds = 0.0f;
    return settings;
}

PlaybackInfo playing(float remaining = 100.0f) { return {Playback::Playing, remaining}; }

void sequentialBiomeTransition()
{
    const std::vector<Track> tracks{track("ice", Cold), track("forest", Verdant)};
    Director director(tracks);
    Settings settings = immediate();
    settings.biomeHoldSeconds = 2.0f;
    settings.minPlaySeconds = 10.0f;
    settings.fadeOutSeconds = 2.0f;
    director.configure(settings);
    CHECK(director.update(20.0f, -1, {}).count == 0);
    CHECK(director.update(1.0f, 0, {}).count == 0);
    auto actions = director.update(1.0f, 0, {});
    CHECK(actions.count == 1 && actions.commands[0].type == CommandType::Play && actions.commands[0].track == 0);
    CHECK(director.update(1.0f, 0, {Playback::Loading, 0}).count == 0);
    CHECK(director.update(0.0f, 0, playing()).count == 0);
    // The candidate biome is held and then adopted, but the piece still gets
    // its minimum playing time. Pausing must not consume that time.
    CHECK(director.update(1.0f, 12, playing()).count == 0);
    CHECK(director.update(1.0f, 12, playing()).count == 0);
    CHECK(director.stableBiome() == 12);
    CHECK(director.update(100.0f, 12, {Playback::Paused, 100}).count == 0);
    CHECK(director.update(7.0f, 12, playing()).count == 0);
    actions = director.update(1.0f, 12, playing());
    CHECK(actions.count == 1 && actions.commands[0].type == CommandType::FadeOut);
    CHECK(director.update(1.0f, 12, playing()).count == 0);
    actions = director.update(0.0f, 12, {});
    CHECK(actions.count == 2 && actions.commands[0].type == CommandType::Release);
    CHECK(actions.commands[1].type == CommandType::Play && actions.commands[1].track == 1);
    director.update(0.0f, 12, playing());
    // Invalid/stale climate samples neither cut the piece nor start a new one.
    CHECK(director.update(100.0f, -1, playing()).count == 0);
    actions = director.update(0.0f, -1, {});
    CHECK(actions.count == 1 && actions.commands[0].type == CommandType::Release);
    CHECK(director.update(100.0f, -1, {}).count == 0);
}

void compatibleBiomesAndBreaths()
{
    const std::vector<Track> tracks{track("ice", Cold)};
    Director director(tracks);
    Settings settings = immediate();
    settings.gapMinSeconds = settings.gapMaxSeconds = 5.0f;
    director.configure(settings);
    CHECK(director.update(0, 0, {}).count == 1);
    director.update(0, 0, playing());
    CHECK(director.update(60, 6, playing()).count == 0); // ColdSteppe, still compatible.
    auto actions = director.update(0, 6, playing(3));
    CHECK(actions.count == 1 && actions.commands[0].seconds == 3);
    actions = director.update(0, 6, {});
    CHECK(actions.count == 1 && actions.commands[0].type == CommandType::Release);
    CHECK(director.update(4, 6, {}).count == 0);
    CHECK(director.update(1, 6, {}).count == 1);
    actions = director.stop();
    CHECK(actions.count == 1 && director.currentTrack() == NoTrack);
    CHECK(director.stop().count == 0);
}

void historyAndVariants()
{
    std::vector<Track> tracks;
    for (int i = 0; i < 8; ++i)
    {
        const std::string id = "composition" + std::to_string(i);
        tracks.push_back(track(id.c_str(), Cold));
    }
    tracks.push_back(track("composition0_loop", Cold, Role::Exploration, "composition0"));
    Director director(tracks, 18);
    director.configure(immediate());
    director.update(0, 0, {});
    std::vector<std::string> last;
    for (int i = 0; i < 50; ++i)
    {
        const auto index = director.currentTrack();
        CHECK(index < tracks.size());
        for (const std::string& recent : last) CHECK(recent != tracks[index].group);
        last.push_back(tracks[index].group);
        if (last.size() > 5) last.erase(last.begin());
        director.update(0, 0, playing());
        const auto actions = director.update(0, 0, {});
        CHECK(actions.count == 2);
    }
    // A pool smaller than the history length must keep playing, alternating
    // compositions rather than starving or repeating variants of one piece.
    const std::vector<Track> small{track("a", Cold), track("a_loop", Cold, Role::Exploration, "a"), track("b", Cold)};
    Director two(small, 3);
    two.configure(immediate());
    two.update(0, 0, {});
    std::string previous;
    for (int i = 0; i < 20; ++i)
    {
        const auto index = two.currentTrack();
        CHECK(index < small.size() && small[index].group != previous);
        previous = small[index].group;
        two.update(0, 0, playing());
        CHECK(two.update(0, 0, {}).count == 2);
    }
    const std::vector<Track> variants{track("a", Cold, Role::Exploration, "a"), track("a_loop", Cold, Role::Exploration, "a")};
    Director one(variants);
    one.configure(immediate());
    one.update(0, 0, {});
    auto prior = NoTrack;
    for (int i = 0; i < 10; ++i)
    {
        CHECK(one.currentTrack() != prior);
        prior = one.currentTrack();
        one.update(0, 0, playing());
        CHECK(one.update(0, 0, {}).count == 2);
    }
}

void weightedFamilies()
{
    std::vector<Track> tracks{track("single", Cold), track("double", Cold | Vast)};
    // Multiple versions of the first composition must not overweight it.
    for (int i = 0; i < 9; ++i) tracks.push_back(track(("variant" + std::to_string(i)).c_str(), Cold, Role::Exploration, "single"));
    int doubles = 0;
    for (unsigned seed = 0; seed < 1000; ++seed)
    {
        Director director(tracks, seed);
        director.configure(immediate());
        director.update(0, 0, {});
        doubles += director.currentTrack() == 1;
    }
    CHECK(doubles > 580 && doubles < 750);
}

void eventsAndCooldowns()
{
    const std::vector<Track> tracks{track("ice", Cold), track("discovery", Mystery, Role::Discovery), track("moon", Lunar, Role::Lunar)};
    Director director(tracks);
    director.configure(immediate());
    director.update(0, 0, {});
    director.update(0, 0, playing());
    CHECK(director.request(Role::Discovery, "ruins", 10, 180, true));
    CHECK(!director.request(Role::Discovery, "lesser", 5));
    auto actions = director.update(0, 0, playing());
    CHECK(actions.count == 1 && actions.commands[0].type == CommandType::FadeOut);
    actions = director.update(0, 0, {});
    CHECK(actions.count == 2 && director.currentTrack() == 1);
    director.update(0, 0, playing());
    CHECK(director.request(Role::Lunar, "fracture", 100, 10));
    CHECK(director.update(0, 0, playing()).commands[0].type == CommandType::FadeOut);
    director.update(0, 0, {});
    CHECK(director.currentTrack() == 2);
    director.update(0, 0, playing());
    CHECK(director.request(Role::Discovery, "stale", 1, 180, false, 1));
    CHECK(director.update(2, 0, playing()).count == 0);
    CHECK(!director.hasPendingEvent());
    director.update(0, 0, {});
    CHECK(director.currentTrack() == 0); // lunar never leaks into exploration.
    CHECK(!director.request(Role::Discovery, "ruins", 100));
    CHECK(!director.request(Role::Lunar, "fracture", 100));
    director.update(8, 0, {Playback::Loading, 0});
    CHECK(director.request(Role::Lunar, "fracture", 100));
    // Cancelling before audio actually starts does not consume a one-shot.
    director.stop();
    CHECK(director.request(Role::Discovery, "not_heard", 10, 180, true));
    director.update(0, -1, {});
    director.stop();
    CHECK(director.request(Role::Discovery, "not_heard", 10, 180, true));
    // A request that expires during loading must never be considered heard.
    director.stop();
    CHECK(director.request(Role::Discovery, "expired_load", 10, 180, true, 1));
    director.update(0, -1, {});
    actions = director.update(2, -1, {Playback::Loading, 0});
    CHECK(actions.count == 1 && actions.commands[0].type == CommandType::Release);
    CHECK(director.request(Role::Discovery, "expired_load", 10, 180, true));
}

void discoveryFallbackAndFailures()
{
    const std::vector<Track> tracks{track("mysterious", Mystery), track("cold", Cold)};
    Director director(tracks, 3);
    director.configure(immediate());
    CHECK(!director.request(Role::Lunar, "moon", 10));
    CHECK(director.request(Role::Discovery, "discovery", 10, 180, true));
    CHECK(director.update(0, -1, {}).count == 1);
    CHECK(director.currentTrack() == 0);
    // Immediate asynchronous failure must not busy-loop through the catalog.
    auto actions = director.update(0, 0, {});
    CHECK(actions.count == 1 && director.unavailable(0));
    for (int i = 0; i < 4; ++i) CHECK(director.update(1, 0, {}).count == 0);
    CHECK(director.update(1, 0, {}).count == 1);
    CHECK(director.currentTrack() == 1); // Retry discovery with local family.
    director.update(0, 0, playing());
    director.stop();
    CHECK(!director.request(Role::Discovery, "discovery", 10));
    // Timeout cancels an orphan load; all failed files stay excluded this session.
    director.reset();
    director.update(0, 0, {});
    const auto failed = director.currentTrack();
    actions = director.update(30, 0, {Playback::Loading, 0});
    CHECK(actions.count == 1 && director.unavailable(failed));
    for (int i = 0; i < 100; ++i) CHECK(director.update(1, 0, {}).count == 0);
    director.reset();
    CHECK(!director.unavailable(failed));
}

void invalidSettings()
{
    const std::vector<Track> tracks{track("ice", Cold)};
    Director director(tracks);
    Settings settings = immediate();
    settings.fadeInSeconds = (std::numeric_limits<float>::infinity)();
    settings.fadeOutSeconds = (std::numeric_limits<float>::quiet_NaN)();
    settings.retryDelaySeconds = -2;
    settings.gapMinSeconds = 2;
    settings.gapMaxSeconds = 1;
    director.configure(settings);
    CHECK(director.settings().fadeInSeconds == 4);
    CHECK(director.settings().fadeOutSeconds == 4);
    CHECK(director.settings().retryDelaySeconds == 0.5f);
    CHECK(director.settings().gapMinSeconds == 1 && director.settings().gapMaxSeconds == 2);
    CHECK(director.update((std::numeric_limits<float>::quiet_NaN)(), -1, {}).count == 0);
    CHECK(director.update(0, 0, {}).count == 1);
}

int main()
{
    sequentialBiomeTransition();
    compatibleBiomesAndBreaths();
    historyAndVariants();
    weightedFamilies();
    eventsAndCooldowns();
    discoveryFallbackAndFailures();
    invalidSettings();
    std::cout << "Music director: lifecycle, families, history, priority, failures and settings passed\n";
}
