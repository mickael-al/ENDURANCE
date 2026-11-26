#include "EnduranceMusicCatalog.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    namespace fs = std::filesystem;
    using namespace EnduranceMusic;

    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
        require(bool(file), "Cannot create fixture");
    }

    void wav(const fs::path& path, unsigned bits = 16)
    {
        std::string bytes;
        const auto u16 = [&](unsigned value)
        { bytes += static_cast<char>(value); bytes += static_cast<char>(value >> 8); };
        const auto u32 = [&](unsigned value)
        { for (unsigned i = 0; i != 4; ++i) bytes += static_cast<char>(value >> (i * 8)); };
        const unsigned dataSize = bits / 8 * 2;
        bytes += "RIFF"; u32(36 + dataSize); bytes += "WAVEfmt "; u32(16);
        u16(1); u16(1); u32(22050); u32(22050 * bits / 8); u16(bits / 8); u16(bits);
        bytes += "data"; u32(dataSize); bytes.append(dataSize, '\0');
        write(path, bytes);
    }

    const Track& find(const Catalog& catalog, std::string_view suffix)
    {
        for (const auto& track : catalog.tracks)
            if (track.id.size() >= suffix.size() &&
                track.id.compare(track.id.size() - suffix.size(), suffix.size(), suffix) == 0) return track;
        throw std::runtime_error("Missing fixture track: " + std::string(suffix));
    }

    bool warningContains(const Catalog& catalog, const std::string& text)
    {
        return std::any_of(catalog.warnings.begin(), catalog.warnings.end(), [&](const std::string& warning)
        { return warning.find(text) != std::string::npos; });
    }

    struct TempDirectory
    {
        fs::path root;
        TempDirectory()
        {
            root = fs::temp_directory_path() / ("endurance-music-catalog-" +
                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            fs::create_directories(root);
        }
        ~TempDirectory() { std::error_code ec; fs::remove_all(root, ec); }
    };

    void catalogueCases(const fs::path& root)
    {
        wav(root / "ELDER WEALD/WAV/01 Forest Dream.wav");
        wav(root / "ELDER WEALD/WAV/01 Forest Dream (Loop).WAV");
        wav(root / "ELDER WEALD/WAV/02 Snowfall (No Drums).wav");
        wav(root / "ELDER WEALD/Duplicate/01 Forest Dream.wav");
        wav(root / "Other Worlds/Dunes.wav");
        wav(root / "Publisher/Ancient Horrors/Broken Moon.wav");
        wav(root / "Other Worlds/Unsupported32.wav", 32);
        write(root / "Other Worlds/Truncated.wav", "RIFF1234WAVEfmt ");
        {
            std::ifstream file(root / "Other Worlds/Dunes.wav", std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            write(root / "Other Worlds/DataBeforeFormat.wav", bytes.substr(0, 12) + bytes.substr(36) + bytes.substr(12, 24));
        }
        write(root / "Other Worlds/Dunes.ogg", "not decoded by the current backend");
        wav(root / "Unrelated/Music.wav");

        const auto catalog = loadCatalog(root.string());
        require(catalog.tracks.size() == 5, "Pack discovery/duplicate encoding/format validation failed");
        require(find(catalog, "01 Forest Dream.wav").group ==
                find(catalog, "01 Forest Dream (Loop).WAV").group, "Versions must share a composition history group");
        require(find(catalog, "02 Snowfall (No Drums).wav").group == "elderweald/snowfall", "No-drums version was not grouped");
        require(find(catalog, "02 Snowfall (No Drums).wav").families == Cold, "Cold keyword classification failed");
        require(find(catalog, "Dunes.wav").families == Vast, "Vast keyword classification failed");
        require(find(catalog, "Broken Moon.wav").role == Role::Lunar, "Ancient Horrors leaked into exploration");
        require(warningContains(catalog, "Unsupported32.wav") && warningContains(catalog, "Truncated.wav"), "Unsupported WAV must be diagnosed");
        require(warningContains(catalog, "DataBeforeFormat.wav"), "Indexer must obey decoder chunk-order requirements");
        require(warningContains(catalog, "PCM WAV"), "Compressed formats must report the required alternative");

        const auto again = loadCatalog(root.string());
        require(again.tracks.size() == catalog.tracks.size(), "Unstable catalogue size");
        for (std::size_t i = 0; i < catalog.tracks.size(); ++i)
            require(catalog.tracks[i].id == again.tracks[i].id, "Catalogue ordering must be deterministic");

        const fs::path manifest = root / "music_catalog.json";
        write(manifest, R"json({"version":1,"tracks":[
            {"id":"ELDER WEALD/WAV/02 Snowfall (No Drums).wav","enabled":false},
            {"path":"elder weald\\wav\\01 forest dream (loop).wav","group":"forest-discovery","families":["Ocean","Mystery"],"role":"Discovery","gain":0.65},
            {"id":"Other Worlds/Dunes.wav","familyMask":1,"gain":-0.2},
            {"id":"Publisher/Ancient Horrors/Broken Moon.wav","role":"exploration","families":["Verdant"],"gain":0.8},
            {"id":"Missing/Unshipped.wav","enabled":false}
        ]})json");
        const auto configured = loadCatalog(root.string(), manifest.string());
        require(configured.tracks.size() == 4, "Disabled track remains playable");
        const auto& discovery = find(configured, "01 Forest Dream (Loop).WAV");
        require(discovery.role == Role::Discovery && discovery.group == "forest-discovery" &&
            discovery.families == (Ocean | Mystery) && discovery.gain == 0.65f, "Override did not replace auto metadata");
        const auto& lunar = find(configured, "Broken Moon.wav");
        require(lunar.role == Role::Lunar && lunar.families == Lunar && lunar.gain == 0.8f,
            "Override must not bypass Ancient Horrors isolation");
        require(find(configured, "Dunes.wav").families == Cold, "Numeric family mask override failed");
        require(find(configured, "Dunes.wav").gain == 1.0f && warningContains(configured, "Invalid music gain"),
            "Invalid gain must retain a safe default");
        require(warningContains(configured, "no matching WAV"), "Stale manifest IDs should be visible");

        write(manifest, "{ invalid JSON");
        const auto malformed = loadCatalog(root.string(), manifest.string());
        require(malformed.tracks.size() == 5 && warningContains(malformed, "Invalid music manifest"),
            "Malformed manifest must preserve the automatic catalogue");

        const auto missing = loadCatalog((root / "missing").string());
        require(missing.tracks.empty() && !missing.warnings.empty(), "Missing music installation must be safe");
        const auto optionalManifest = loadCatalog(root.string(), (root / "not-shipped.json").string());
        require(optionalManifest.tracks.size() == 5, "Optional manifest should not be required");
    }

    void biomeCases()
    {
        for (int i = 0; i < 26; ++i)
            require(biomeFamilies(i) != 0 && !(biomeFamilies(i) & Lunar), "Biome missing family or enables lunar exploration");
        require(biomeFamilies(-1) == 0 && biomeFamilies(26) == 0, "Invalid biome should not select a family");
        require((biomeFamilies(25) & Ocean) != 0, "Marine must use ocean music");
        require(biomeFamilies(12) == Verdant, "Temperate forest should not choose arid music");
    }

    void suffixCases(const fs::path& root)
    {
        wav(root / "Otherworlds/The Long Road.wav");
        wav(root / "Otherworlds/The Long Road (Full Version 2).wav");
        wav(root / "Otherworlds/The Long Night.wav");
        wav(root / "Otherworlds/The Long Night - No Choir.wav");
        wav(root / "Elderweald/Ancient Horrors/Hidden Prophecy.wav");
        wav(root / "Elderweald/Frostbloom.wav", 24);
        const auto catalog = loadCatalog(root.string());
        require(find(catalog, "Frostbloom.wav").role == Role::Exploration,
            "The shipped 24-bit Elderweald pack must be indexed for playback");
        require(find(catalog, "The Long Road.wav").group == find(catalog, "The Long Road (Full Version 2).wav").group,
            "Full mix should share composition history");
        require(find(catalog, "The Long Night.wav").group == find(catalog, "The Long Night - No Choir.wav").group,
            "Instrument mix should share composition history");
        require(find(catalog, "The Long Road.wav").group != find(catalog, "The Long Night.wav").group,
            "Ordinary title words must not be mistaken for version suffixes");
        require(find(catalog, "Hidden Prophecy.wav").role == Role::Lunar,
            "Nested Ancient Horrors must remain excluded from exploration");
    }
}

int main()
{
    try
    {
        TempDirectory fixtures;
        catalogueCases(fixtures.root);
        biomeCases();
        suffixCases(fixtures.root / "suffixes");
        std::cout << "Music catalogue tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
