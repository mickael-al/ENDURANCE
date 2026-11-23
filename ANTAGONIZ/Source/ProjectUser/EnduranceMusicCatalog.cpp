#include "EnduranceMusicCatalog.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "json_struct.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace EnduranceMusic
{
namespace
{
    namespace fs = std::filesystem;
    constexpr std::uint64_t MaxManifestBytes = 4u * 1024u * 1024u;
    constexpr std::uint64_t MaxWaveBytes = 512u * 1024u * 1024u;

    struct Override
    {
        std::string id;
        // path is an alias convenient when authoring manifests by hand.
        std::string path;
        bool enabled = true;
        std::string group;
        std::vector<std::string> families;
        FamilyMask familyMask = 0;
        std::string role;
        float gain = -1.0f;
        JS_OBJ(id, path, enabled, group, families, familyMask, role, gain);
    };

    struct Manifest
    {
        int version = 1;
        std::vector<Override> tracks;
        JS_OBJ(version, tracks);
    };

    std::string lower(std::string text)
    {
        for (char& c : text)
        {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        }
        return text;
    }

    std::string compact(const std::string& text)
    {
        std::string result;
        for (unsigned char c : lower(text))
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                result += static_cast<char>(c);
        return result;
    }

    std::vector<std::string> words(const std::string& text)
    {
        std::vector<std::string> result;
        std::string word;
        for (unsigned char c : lower(text))
        {
            if (std::isalnum(c) || c >= 128) word += static_cast<char>(c);
            else if (!word.empty()) { result.push_back(std::move(word)); word.clear(); }
        }
        if (!word.empty()) result.push_back(std::move(word));
        return result;
    }

    bool digits(const std::string& value)
    {
        return !value.empty() && value.find_first_not_of("0123456789") == std::string::npos;
    }

    std::string packOf(const fs::path& relative)
    {
        std::string pack;
        for (const auto& part : relative)
        {
            const auto name = compact(part.u8string());
            if (name == "ancienthorrors") return name;
            if (pack.empty() && (name == "elderweald" || name == "otherworlds")) pack = name;
        }
        return pack;
    }

    std::string compositionGroup(const std::string& pack, const std::string& stem)
    {
        auto tokens = words(stem);
        // Leading track numbers are packaging, not part of the composition ID.
        while (tokens.size() > 1 && digits(tokens.front())) tokens.erase(tokens.begin());
        // Conservative suffix vocabulary: ordinary title words stay intact.
        // A hand-authored group always takes precedence for unusual pack names.
        static const std::set<std::string> variants{
            "loop", "looping", "loopable", "full", "short", "long", "extended",
            "instrumental", "alternate", "alternative", "alt", "edit", "version",
            "variation", "mix", "master", "stereo", "mono", "16bit", "24bit", "32bit"
        };
        static const std::set<std::string> variantQualifiers{
            "drums", "percussion", "melody", "vocals", "choir", "cello", "flute", "bass", "piano"
        };
        const auto versionNumber = [](const std::string& token)
        { return token.size() > 1 && token[0] == 'v' && digits(token.substr(1)); };
        std::size_t end = tokens.size();
        for (std::size_t i = 1; i < tokens.size(); ++i)
        {
            if (variants.count(tokens[i]) || versionNumber(tokens[i]) ||
                (tokens[i] == "no" && i + 1 < tokens.size() &&
                 variantQualifiers.count(tokens[i + 1])))
            {
                // Only strip an actual variant suffix: "The Long Road" and
                // "Full Moon Rising" must not collapse to generic title IDs.
                bool suffix = true;
                for (std::size_t j = i; j < tokens.size(); ++j)
                    if (!variants.count(tokens[j]) && !variantQualifiers.count(tokens[j]) &&
                        tokens[j] != "no" && !digits(tokens[j]) && !versionNumber(tokens[j]))
                    { suffix = false; break; }
                if (suffix) { end = i; break; }
            }
        }
        std::string group = pack + '/';
        for (std::size_t i = 0; i < end; ++i)
        {
            if (i) group += ' ';
            group += tokens[i];
        }
        if (end == 0) group += lower(stem);
        return group;
    }

    bool anyWord(const std::vector<std::string>& tokens, std::initializer_list<const char*> candidates)
    {
        for (const char* candidate : candidates)
            if (std::find(tokens.begin(), tokens.end(), candidate) != tokens.end()) return true;
        return false;
    }

    FamilyMask inferFamilies(const std::string& pack, const std::string& stem)
    {
        if (pack == "ancienthorrors") return Lunar;
        const auto tokens = words(stem);
        FamilyMask result = 0;
        if (anyWord(tokens, {"snow", "snowfall", "frost", "frozen", "ice", "icy", "winter", "cold", "tundra", "glacier"}))
            result |= Cold;
        if (anyWord(tokens, {"forest", "woods", "woodland", "grove", "meadow", "bloom", "garden", "spring", "moss", "leaves", "canopy"}))
            result |= Verdant;
        if (anyWord(tokens, {"desert", "dunes", "sand", "sands", "wilderness", "plain", "plains", "steppe", "horizon", "mountain", "mountains", "journey"}))
            result |= Vast;
        if (anyWord(tokens, {"ocean", "sea", "shore", "shores", "coast", "tide", "tides", "island", "islands", "waves", "water", "river"}))
            result |= Ocean;
        if (anyWord(tokens, {"ancient", "mystery", "mysterious", "ruins", "ruin", "dream", "dreams", "forgotten", "strange", "enchanted", "ethereal", "cavern", "abyss", "whispers"}))
            result |= Mystery;
        if (result != 0) return result;
        return pack == "elderweald" ? (Verdant | Mystery) : (Cold | Vast | Mystery | Ocean);
    }

    bool finiteGain(float gain)
    {
        // Release builds use -ffast-math, which can optimize isfinite away.
        std::uint32_t bits = 0;
        static_assert(sizeof bits == sizeof gain, "32-bit float expected");
        std::memcpy(&bits, &gain, sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    std::uint32_t u32(const unsigned char* bytes)
    {
        return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
               (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    }

    std::uint16_t u16(const unsigned char* bytes)
    {
        return std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8);
    }

    bool playableWave(const fs::path& path)
    {
        // Only read small chunk headers. Never decode or load PCM in the indexer.
        std::ifstream file(path, std::ios::binary);
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end < 12) return false;
        const auto size = static_cast<std::uint64_t>(end);
        file.seekg(0);
        unsigned char header[12]{};
        if (!file.read(reinterpret_cast<char*>(header), sizeof header) ||
            std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) return false;
        bool formatValid = false;
        std::uint16_t blockAlign = 0;
        std::uint64_t position = 12;
        for (unsigned chunks = 0; chunks < 4096 && position + 8 <= size; ++chunks)
        {
            file.seekg(static_cast<std::streamoff>(position));
            unsigned char chunk[8]{};
            if (!file.read(reinterpret_cast<char*>(chunk), sizeof chunk)) return false;
            const auto length = u32(chunk + 4);
            position += 8;
            if (length > size - position) return false;
            if (std::memcmp(chunk, "fmt ", 4) == 0)
            {
                unsigned char format[16]{};
                if (length < sizeof format || !file.read(reinterpret_cast<char*>(format), sizeof format)) return false;
                const auto channels = u16(format + 2), bits = u16(format + 14);
                blockAlign = u16(format + 12);
                formatValid = u16(format) == 1 && (channels == 1 || channels == 2) &&
                    (bits == 8 || bits == 16) && u32(format + 4) != 0 &&
                    blockAlign == channels * (bits / 8);
                if (!formatValid) return false;
            }
            else if (std::memcmp(chunk, "data", 4) == 0)
                // Match SoundTypeLoader: data must follow a supported fmt chunk.
                return formatValid && length != 0 && length <= MaxWaveBytes && length % blockAlign == 0;
            position += std::uint64_t(length) + (length & 1u);
        }
        return false;
    }

    Manifest readManifest(const std::string& path, std::vector<std::string>& warnings)
    {
        Manifest manifest;
        if (path.empty()) return manifest;
        std::error_code ec;
        if (!fs::exists(fs::u8path(path), ec))
        {
            if (ec) warnings.push_back("Cannot access music manifest: " + path + " (" + ec.message() + ")");
            return manifest;
        }
        const auto size = fs::file_size(fs::u8path(path), ec);
        if (ec || size > MaxManifestBytes)
        { warnings.push_back("Music manifest is unreadable or too large: " + path); return manifest; }
        std::ifstream file(fs::u8path(path), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!file || text.empty())
        { warnings.push_back("Cannot read music manifest: " + path); return manifest; }
        JS::ParseContext context(text);
        if (context.parseTo(manifest) != JS::Error::NoError || manifest.version != 1)
        {
            warnings.push_back("Invalid music manifest (expected version 1): " + path);
            return {};
        }
        return manifest;
    }

    void applyOverride(Track& track, const Override& entry, std::vector<std::string>& warnings)
    {
        if (!entry.group.empty()) track.group = entry.group;
        if (!entry.role.empty())
        {
            const auto role = compact(entry.role);
            if (role == "exploration") track.role = Role::Exploration;
            else if (role == "discovery") track.role = Role::Discovery;
            else if (role == "lunar") track.role = Role::Lunar;
            else warnings.push_back("Unknown music role '" + entry.role + "' for " + track.id);
        }
        FamilyMask families = entry.familyMask & AllFamilies;
        if (entry.familyMask != families)
            warnings.push_back("Unknown music family bits ignored for " + track.id);
        for (const auto& name : entry.families)
        {
            const auto value = compact(name);
            if (value == "cold") families |= Cold;
            else if (value == "verdant") families |= Verdant;
            else if (value == "vast") families |= Vast;
            else if (value == "mystery") families |= Mystery;
            else if (value == "ocean") families |= Ocean;
            else if (value == "lunar") families |= Lunar;
            else warnings.push_back("Unknown music family '" + name + "' for " + track.id);
        }
        if (families != 0) track.families = families;
        if (!finiteGain(entry.gain) || (entry.gain < 0.0f && entry.gain != -1.0f) || entry.gain > 1.0f)
            warnings.push_back("Invalid music gain (expected 0..1) for " + track.id);
        else if (entry.gain >= 0.0f) track.gain = entry.gain;
    }
}

FamilyMask biomeFamilies(int biome)
{
    static constexpr std::array<FamilyMask, 26> families{{
        Cold | Vast,                 // PolarIce
        Cold | Vast,                 // FrozenTundra
        Cold | Vast,                 // Tundra
        Cold | Verdant,              // Taiga
        Cold | Verdant | Mystery,    // BorealBog
        Cold | Vast,                 // RockyTundra
        Cold | Vast,                 // ColdSteppe
        Cold | Verdant,              // BorealForest
        Cold | Verdant | Mystery,    // ColdForest
        Cold | Mystery,              // ColdMarsh
        Vast,                        // DryPlain
        Verdant | Vast,              // Meadow
        Verdant,                     // TemperateForest
        Verdant | Mystery,           // TemperateRainforest
        Verdant | Mystery,           // TemperateMarsh
        Vast,                        // RockDesert
        Vast | Verdant,              // DrySavanna
        Verdant | Vast,              // Savanna
        Verdant | Mystery,           // DryJungle
        Verdant | Mystery,           // TropicalJungle
        Vast | Mystery,              // VolcanicDesert
        Vast | Mystery,              // RedDesert
        Vast,                        // AridPlateau
        Verdant | Mystery,           // BurningJungle
        Verdant | Ocean | Mystery,   // TropicalMangrove
        Ocean | Vast                 // Marine
    }};
    static_assert(families.size() == ProceduralTerrain::BiomeCount, "Update music biome families");
    return biome >= 0 && static_cast<std::size_t>(biome) < families.size() ? families[biome] : 0;
}

const char* familyName(Family family)
{
    switch (family)
    {
    case Cold: return "Cold";
    case Verdant: return "Verdant";
    case Vast: return "Vast";
    case Mystery: return "Mystery";
    case Ocean: return "Ocean";
    case Lunar: return "Lunar";
    default: return "Unknown";
    }
}

const char* roleName(Role role)
{
    switch (role)
    {
    case Role::Exploration: return "Exploration";
    case Role::Discovery: return "Discovery";
    case Role::Lunar: return "Lunar";
    default: return "Unknown";
    }
}

Catalog loadCatalog(const std::string& root, const std::string& configFile)
{
    Catalog result;
    const auto manifest = readManifest(configFile, result.warnings);
    std::map<std::string, const Override*> overrides;
    for (const auto& entry : manifest.tracks)
    {
        const auto id = lower(entry.id.empty() ? entry.path : entry.id);
        if (id.empty()) { result.warnings.push_back("Music override is missing a root-relative id"); continue; }
        if (!overrides.emplace(id, &entry).second)
            result.warnings.push_back("Duplicate music override ignored: " + id);
    }
    std::set<std::string> matchedOverrides;
    const fs::path directory = fs::u8path(root).lexically_normal();
    std::error_code ec;
    if (root.empty() || !fs::is_directory(directory, ec))
    { result.warnings.push_back("Music folder not found: " + root); return result; }

    std::vector<fs::path> files;
    std::size_t unsupportedCount = 0;
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end)
    {
        const auto& entry = *it;
        // Avoid following directory symlinks (the default iterator policy).
        std::error_code typeError;
        if (entry.is_regular_file(typeError))
        {
            const auto relative = entry.path().lexically_relative(directory);
            if (!packOf(relative.parent_path()).empty())
            {
                const auto extension = lower(entry.path().extension().u8string());
                if (extension == ".wav") files.push_back(entry.path());
                else if (extension == ".ogg" || extension == ".mp3" || extension == ".flac" || extension == ".aif" || extension == ".aiff")
                    ++unsupportedCount;
            }
        }
        it.increment(ec);
    }
    if (ec) result.warnings.push_back("Music scan incomplete: " + ec.message());
    if (unsupportedCount)
        result.warnings.push_back(std::to_string(unsupportedCount) + " compressed/unsupported music files skipped: AudioManager currently requires PCM WAV (8/16-bit mono/stereo)");
    std::sort(files.begin(), files.end());
    std::set<std::string> duplicateIds;
    for (const auto& file : files)
    {
        const auto relative = file.lexically_relative(directory);
        Track track;
        track.id = relative.generic_u8string();
        track.path = file.generic_u8string();
        const auto pack = packOf(relative.parent_path());
        const auto stem = file.stem().u8string();
        track.group = compositionGroup(pack, stem);
        track.families = inferFamilies(pack, stem);
        const auto overrideIt = overrides.find(lower(track.id));
        if (overrideIt != overrides.end())
        {
            matchedOverrides.insert(overrideIt->first);
            if (!overrideIt->second->enabled) continue;
            applyOverride(track, *overrideIt->second, result.warnings);
        }
        if (pack == "ancienthorrors")
        {
            if (overrideIt != overrides.end() && !overrideIt->second->role.empty() && track.role != Role::Lunar)
                result.warnings.push_back("Ancient Horrors remains lunar-only: " + track.id);
            track.role = Role::Lunar;
            track.families = Lunar;
        }
        if (!playableWave(file))
        { result.warnings.push_back("Unsupported or invalid PCM WAV skipped: " + track.id); continue; }
        // Repeated format-export subfolders often contain the same named file.
        // Explicitly configured copies remain independent; variants still share
        // a composition group for the director's recent-history exclusion.
        const auto duplicate = pack + '/' + compact(stem);
        if (overrideIt == overrides.end() && !duplicateIds.insert(duplicate).second) continue;
        result.tracks.push_back(std::move(track));
    }
    for (const auto& entry : overrides)
        if (!matchedOverrides.count(entry.first)) result.warnings.push_back("Music override has no matching WAV: " + entry.first);
    if (result.tracks.empty()) result.warnings.push_back("No playable music found in Elderweald, Otherworlds or Ancient Horrors below " + root);
    return result;
}
}
