#include "EnduranceTerrainMaterialSerialization.hpp"
#include "EnduranceTerrainMaterialMigration.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

struct SceneAppearance
{
    ProceduralTerrain::TerrainAppearance appearance = Endurance::makeTerrainAppearance();
    std::uint32_t terrainAppearanceRevision = 0;
};
JS_OBJ_EXT(SceneAppearance, appearance, terrainAppearanceRevision);

namespace
{
bool sameTable(const ProceduralTerrain::TerrainAppearance& a,
    const ProceduralTerrain::TerrainAppearance& b)
{
    return std::memcmp(a.biomes.data(), b.biomes.data(), sizeof(a.biomes)) == 0;
}
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1]);
    assert(input);
    const std::string legacy((std::istreambuf_iterator<char>(input)), {});
    const auto defaults = Endurance::makeTerrainAppearance();
    const auto decodeLegacy = [&]()
    {
        SceneAppearance scene;
        JS::ParseContext context(legacy);
        assert(context.parseTo(scene) == JS::Error::NoError);
        assert(ProceduralTerrain::validTerrainAppearance(scene.appearance));
        assert(scene.terrainAppearanceRevision == 0);
        // Missing v2 fields must inherit this biome's authored controls,
        // rather than a generic fallback or another biome's settings.
        for (std::size_t i = 0; i < ProceduralTerrain::BiomeCount; ++i)
        {
            assert(scene.appearance.biomes[i].spectrum == defaults.biomes[i].spectrum);
            assert(scene.appearance.biomes[i].ground.normal == defaults.biomes[i].ground.normal);
            assert(scene.appearance.biomes[i].wall.normal == defaults.biomes[i].wall.normal);
            assert(scene.appearance.biomes[i].ground.base.fineSurface == defaults.biomes[i].ground.base.fineSurface);
            assert(scene.appearance.biomes[i].ground.accent.fineSurface == defaults.biomes[i].ground.accent.fineSurface);
        }
        return scene;
    };

    // Genuine old factory JSON captured from 426d806, not recomputed from the
    // implementation under test. Every old preset must be recognized.
    auto unedited = decodeLegacy();
    assert(Endurance::upgradeTerrainAppearance(unedited.appearance,
        unedited.terrainAppearanceRevision) == ProceduralTerrain::BiomeCount);
    assert(sameTable(unedited.appearance, defaults));
    assert(unedited.terrainAppearanceRevision == Endurance::TerrainAppearanceRevision);

    // Preserve whole artist-edited biomes and all independent global settings.
    auto customized = decodeLegacy();
    customized.appearance.biomes[2].ground.base.colorMid[1] += 0.004f;
    customized.appearance.biomes[20].wall.detail[0] += 4.0f;
    customized.appearance.macroPeriod = 687.0f;
    customized.appearance.patchPeriod = 71.0f;
    customized.appearance.debugView = 2;
    auto before = customized.appearance;
    Endurance::configureTerrainFineMaterials(before);
    assert(Endurance::upgradeTerrainAppearance(customized.appearance,
        customized.terrainAppearanceRevision) == ProceduralTerrain::BiomeCount - 2);
    for (std::size_t i = 0; i < ProceduralTerrain::BiomeCount; ++i)
    {
        const auto& expected = i == 2 || i == 20 ? before.biomes[i] : defaults.biomes[i];
        assert(std::memcmp(&customized.appearance.biomes[i], &expected, sizeof(expected)) == 0);
    }
    assert(customized.appearance.macroPeriod == before.macroPeriod);
    assert(customized.appearance.patchPeriod == before.patchPeriod);
    assert(customized.appearance.debugView == before.debugView);

    // Saved v2 scenes never get remigrated, even if a user deliberately uses
    // an old palette. Future revisions also remain untouched.
    auto current = decodeLegacy();
    current.terrainAppearanceRevision = 2;
    current.appearance.biomes[0].wall.base.colorLow[3] = 0.04f;
    auto v2Before = current.appearance;
    Endurance::configureTerrainFineMaterials(v2Before);
    assert(Endurance::upgradeTerrainAppearance(current.appearance,
        current.terrainAppearanceRevision) == 0);
    assert(sameTable(current.appearance, v2Before));
    assert(current.terrainAppearanceRevision == Endurance::TerrainAppearanceRevision);
    current.terrainAppearanceRevision = Endurance::TerrainAppearanceRevision;
    const auto currentBefore = current.appearance;
    assert(Endurance::upgradeTerrainAppearance(current.appearance,
        current.terrainAppearanceRevision) == 0);
    assert(sameTable(current.appearance, currentBefore));
    current.terrainAppearanceRevision = Endurance::TerrainAppearanceRevision + 1;
    assert(Endurance::upgradeTerrainAppearance(current.appearance,
        current.terrainAppearanceRevision) == 0);
    assert(sameTable(current.appearance, currentBefore));

    // A real v3 saved scene contains the OLD fine controls. Replace untouched
    // values but preserve user changes, blue-ice roughness and global scales.
    auto v3 = defaults;
    v3.biomes[0].ground.base.fineSurface = {{.90f,.15f,0.f,0.f}};
    v3.biomes[1].ground.base.fineSurface = {{.55f,.18f,0.f,0.f}};
    v3.biomes[1].ground.accent.fineSurface = {{0.f,.16f,0.f,0.f}};
    v3.biomes[1].wall.accent.fineSurface = {{0.f,.16f,0.f,0.f}};
    v3.biomes[0].wall.base.colorLow[3] = .04f;
    v3.biomes[12].ground.base.fineSurface[2] = .23f;
    v3.biomes[12].ground.base.colorMid[1] = .013f;
    v3.biomes[12].stainRouting = {{0.f,0.f,0.f,0.f}};
    v3.vegetationStainSize = 7.f;
    auto classification = v3;
    Endurance::configureTerrainFineMaterials(classification);
    std::uint32_t revision = 3;
    Endurance::upgradeTerrainAppearance(v3,revision);
    assert(revision == 4);
    assert(v3.biomes[0].ground.base.fineSurface == defaults.biomes[0].ground.base.fineSurface);
    assert(v3.biomes[1].ground.base.fineSurface[0] == 0.f);
    assert(v3.biomes[1].ground.accent.fineSurface[0] > 0.f);
    assert(v3.biomes[1].wall.accent.fineSurface[0] > 0.f);
    assert(v3.biomes[0].wall.base.colorLow[3] == .04f);
    assert(v3.biomes[12].ground.base.fineSurface[2] == .23f);
    assert(v3.biomes[12].ground.base.colorMid[1] == .013f);
    assert(v3.biomes[12].stainRouting == classification.biomes[12].stainRouting);
    assert(v3.vegetationStainSize == 7.f);

    // Pre-material scenes have neither appearance nor version metadata.
    SceneAppearance absent;
    JS::ParseContext missing("{\"seed\":1337}");
    assert(missing.parseTo(absent) == JS::Error::NoError);
    Endurance::upgradeTerrainAppearance(absent.appearance, absent.terrainAppearanceRevision);
    assert(sameTable(absent.appearance, defaults));
    assert(absent.terrainAppearanceRevision == Endurance::TerrainAppearanceRevision);

    auto invalid = decodeLegacy();
    invalid.appearance.biomes[0].ground.base.colorLow[0] =
        (std::numeric_limits<float>::quiet_NaN)();
    assert(Endurance::upgradeTerrainAppearance(invalid.appearance,
        invalid.terrainAppearanceRevision) == 0);
    assert(invalid.terrainAppearanceRevision == 0);
    assert(!ProceduralTerrain::validTerrainAppearance(invalid.appearance));
    JS::ParseContext badJson("{\"appearance\":{\"patchPeriod\":\"bad\"}}");
    assert(badJson.parseTo(invalid) != JS::Error::NoError);

    const auto saved = JS::serializeStruct(customized);
    SceneAppearance restored;
    JS::ParseContext roundtrip(saved);
    assert(roundtrip.parseTo(restored) == JS::Error::NoError);
    assert(restored.terrainAppearanceRevision == Endurance::TerrainAppearanceRevision);
    assert(sameTable(restored.appearance, customized.appearance));
    assert(Endurance::upgradeTerrainAppearance(restored.appearance,
        restored.terrainAppearanceRevision) == 0);
    std::cout << "PASS old-preset migration, artist edits/global settings, missing fields, "
                 "invalid data, current/future versions and saved scene roundtrip\n";
}
