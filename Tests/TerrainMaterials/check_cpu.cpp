#include "ProceduralTerrainMaterials.hpp"
#include "EnduranceTerrainMaterials.hpp"
#include "EnduranceTerrainMaterialSerialization.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>

namespace
{
using namespace ProceduralTerrain;

void validate()
{
    const TerrainAppearance neutral;
    assert(validTerrainAppearance(neutral));
    const auto authored = Endurance::makeTerrainAppearance();
    assert(validTerrainAppearance(authored));
    assert(validTerrainMaterialTable(authored.biomes));
    assert(sizeof(TerrainMaterial) == 96);
    assert(sizeof(TerrainSurface) == 240);
    assert(sizeof(TerrainBiomeAppearance) == 512);
    assert(sizeof(TerrainMaterialTable) == 13312);

    // Distinct authored palettes, not 26 copies of a generic fallback. Each
    // biome must also distinguish its surface base from its accent and walls.
    std::set<std::string> palettes;
    for (const auto& biome : authored.biomes)
    {
        const auto bytes = reinterpret_cast<const char*>(&biome);
        palettes.emplace(bytes, sizeof(biome));
        assert(std::memcmp(&biome.ground.base, &biome.ground.accent,
                           sizeof(TerrainMaterial)) != 0);
        assert(std::memcmp(&biome.wall.base, &biome.wall.accent,
                           sizeof(TerrainMaterial)) != 0);
        assert(std::memcmp(&biome.ground, &biome.wall, sizeof(TerrainSurface)) != 0);
        for (const auto* surface : {&biome.ground, &biome.wall})
        for (const auto* material : {&surface->base, &surface->accent})
        {
            // These are natural dielectric presets; metallic remains an
            // editable capability, not a shortcut to making snow reflective.
            assert(material->response[0] == 0.0f);
            assert(material->response[1] > 0.0f);
        }
    }
    assert(palettes.size() == 26);
    const auto again = Endurance::makeTerrainAppearance();
    assert(std::memcmp(authored.biomes.data(), again.biomes.data(),
                       sizeof(TerrainMaterialTable)) == 0);

    auto reject = [&](auto mutate)
    {
        auto invalid = authored;
        mutate(invalid);
        assert(!validTerrainAppearance(invalid));
        // Validation must not mutate the caller's invalid value or source.
        assert(validTerrainAppearance(authored));
    };
    reject([](auto& a) { a.biomes[0].ground.base.colorLow[0] = -0.1f; });
    reject([](auto& a) { a.biomes[0].ground.base.colorMid[3] = 2.0f; });
    reject([](auto& a) { a.biomes[0].wall.accent.response[0] = -1.0f; });
    reject([](auto& a) { a.biomes[0].wall.accent.response[1] = 2.0f; });
    reject([](auto& a) { a.biomes[0].wall.accent.response[2] = -1.0f; });
    reject([](auto& a) { a.biomes[0].ground.detail[0] = 0.0f; });
    reject([](auto& a) { a.biomes[0].ground.normal[0] = 1.5f; });
    reject([](auto& a) { a.biomes[0].ground.normal[0] = 8.0f; });
    reject([](auto& a) { a.biomes[0].wall.normal[1] = 0.0f; });
    reject([](auto& a) { a.biomes[0].ground.normal[2] = 2.1f; });
    reject([](auto& a) { a.biomes[0].wall.normal[3] = -0.1f; });
    reject([](auto& a) { a.biomes[0].spectrum[0] = 0.0f; });
    reject([](auto& a) { a.biomes[0].spectrum[1] = 0.0f; });
    reject([](auto& a) { a.biomes[0].spectrum[2] = 4.0f; });
    reject([](auto& a) { a.biomes[0].spectrum[3] = 0.6f; });
    reject([](auto& a) { a.biomes[0].slope[1] = a.biomes[0].slope[0]; });
    reject([](auto& a) { a.macroPeriod = 0.0f; });
    reject([](auto& a) { a.patchPeriod = -1.0f; });
    reject([](auto& a) { a.debugView = -1; });
    reject([](auto& a) { a.debugView = 100; });
    reject([](auto& a) {
        a.biomes[25].wall.accent.colorHigh[2] =
            (std::numeric_limits<float>::quiet_NaN)();
    });
    reject([](auto& a) {
        a.biomes[13].ground.base.pattern[3] =
            (std::numeric_limits<float>::infinity)();
    });
    reject([](auto& a) {
        a.patchPeriod = (std::numeric_limits<float>::infinity)();
    });

    // Actual nonzero metallic input must be accepted even though the natural
    // default palettes do not use it. GPU tests verify that it reaches gOther.
    auto fractionalOctaves = authored;
    fractionalOctaves.biomes[0].spectrum[2] = 2.5f;
    assert(validTerrainAppearance(fractionalOctaves));
    auto metal = authored;
    metal.biomes[0].ground.base.response[0] = 0.75f;
    assert(validTerrainAppearance(metal));

    // Real game serialization must preserve artist edits, including controls
    // absent from old scenes, without putting JSON dependencies in the engine.
    auto edited = metal;
    edited.macroPeriod = 631.5f;
    edited.patchPeriod = 17.25f;
    edited.debugView = 3;
    edited.biomes[25].wall.accent.colorHigh = {{.123f,.456f,.789f,.321f}};
    edited.biomes[13].ground.base.pattern = {{.15f,.27f,.42f,.16f}};
    edited.biomes[13].ground.normal = {{7.0f, 1.75f, 0.63f, 0.27f}};
    edited.biomes[25].spectrum = {{1.3f, 0.11f, 3.0f, 0.42f}};
    const std::string json = JS::serializeStruct(edited);
    TerrainAppearance decoded;
    JS::ParseContext roundtrip(json);
    assert(roundtrip.parseTo(decoded) == JS::Error::NoError);
    assert(validTerrainAppearance(decoded));
    std::array<float,sizeof(TerrainMaterialTable)/sizeof(float)> before{},after{};
    std::memcpy(before.data(),edited.biomes.data(),sizeof(TerrainMaterialTable));
    std::memcpy(after.data(),decoded.biomes.data(),sizeof(TerrainMaterialTable));
    for (std::size_t i = 0; i < before.size(); ++i)
        assert(std::abs(before[i]-after[i]) < 1e-6f);
    assert(decoded.macroPeriod == edited.macroPeriod);
    assert(decoded.patchPeriod == edited.patchPeriod);
    assert(decoded.debugView == edited.debugView);
    TerrainAppearance oldScene;
    const auto original = oldScene;
    JS::ParseContext legacy("{\"obsoleteLegacySetting\":12}");
    assert(legacy.parseTo(oldScene) == JS::Error::NoError);
    assert(std::memcmp(oldScene.biomes.data(),original.biomes.data(),sizeof(TerrainMaterialTable)) == 0);
    assert(validTerrainAppearance(oldScene));
    JS::ParseContext invalidJson("{\"macroPeriod\":\"not a number\"}");
    assert(invalidJson.parseTo(oldScene) != JS::Error::NoError);
}
}

int main(int argc, char** argv)
{
    validate();
    if (argc == 2 && std::string(argv[1]) == "--dump")
    {
        const auto appearance = Endurance::makeTerrainAppearance();
        std::cout.write(reinterpret_cast<const char*>(appearance.biomes.data()),
                        sizeof(ProceduralTerrain::TerrainMaterialTable));
        return std::cout.good() ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--settings")
    {
        const auto appearance = Endurance::makeTerrainAppearance();
        std::cout << appearance.macroPeriod << ' ' << appearance.patchPeriod
                  << ' ' << appearance.debugView << '\n';
        return 0;
    }
    std::cout << "PASS material std140 ABI, 26 authored palettes, dielectric defaults, "
                 "determinism, finite/range validation, metallic capability and game JSON roundtrip\n";
}
