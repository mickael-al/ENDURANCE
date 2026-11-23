#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Game-owned catalogue metadata; independent of OpenAL, rendering and scenes.
namespace EnduranceMusic
{
    using FamilyMask = std::uint32_t;
    enum Family : FamilyMask
    {
        Cold = 1u << 0,
        Verdant = 1u << 1,
        Vast = 1u << 2,
        Mystery = 1u << 3,
        Ocean = 1u << 4,
        Lunar = 1u << 5
    };
    constexpr FamilyMask AllFamilies = Cold | Verdant | Vast | Mystery | Ocean | Lunar;

    enum class Role { Exploration, Discovery, Lunar };

    struct Track
    {
        // Stable root-relative path, including pack and extension. Use this ID
        // in the optional manifest. path is the actual playable filesystem path.
        std::string id;
        std::string path;
        // Versions of the same composition share a history/cooldown group.
        std::string group;
        FamilyMask families = 0;
        Role role = Role::Exploration;
        float gain = 1.0f;
    };

    struct Catalog
    {
        std::vector<Track> tracks;
        std::vector<std::string> warnings;
    };

    // Performs the only filesystem scan. No decoding or OpenAL allocation.
    // Pack names are recognized case/space-insensitively anywhere below root.
    // The engine supports RIFF PCM WAV (8/16/24-bit, mono/stereo) only.
    // 24-bit sources are converted to dithered 16-bit PCM by the audio loader.
    // Missing files/invalid entries produce warnings and never block startup.
    Catalog loadCatalog(const std::string& root, const std::string& configFile = {});

    // All 26 ProceduralTerrain biome IDs have explicit family associations.
    // Invalid IDs return zero; lunar tracks are never exploration candidates.
    FamilyMask biomeFamilies(int biome);
    const char* familyName(Family family);
    const char* roleName(Role role);
}
