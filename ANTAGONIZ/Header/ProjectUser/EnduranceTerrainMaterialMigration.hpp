#pragma once

#include "EnduranceTerrainMaterials.hpp"
#include <cmath>
#include <cstdint>

namespace Endurance
{
    // Scene data version, deliberately outside the renderer's std140 records.
    constexpr std::uint32_t TerrainAppearanceRevision = 4;

    inline std::uint64_t terrainAppearanceLegacyFingerprint(
        const ProceduralTerrain::TerrainBiomeAppearance& biome) noexcept
    {
        // Only the v1 fields participate. New normal/spectrum controls already
        // inherit the current biome defaults before parsing a legacy scene.
        // Quantization tolerates insignificant JSON/libm differences. A miss
        // is conservative: the artist's complete old biome is kept unchanged.
        std::uint64_t hash = 14695981039346656037ull;
        const auto append = [&](const ProceduralTerrain::TerrainMaterialVector& v)
        {
            for (float value : v)
            {
                const auto quantized = static_cast<std::uint64_t>(
                    std::llround(static_cast<double>(value) * 1000000.0));
                for (unsigned int byte = 0; byte < 8; ++byte)
                {
                    hash ^= (quantized >> (byte * 8)) & 0xffu;
                    hash *= 1099511628211ull;
                }
            }
        };
        for (const auto* surface : {&biome.ground, &biome.wall})
        {
            for (const auto* material : {&surface->base, &surface->accent})
            {
                append(material->colorLow);
                append(material->colorMid);
                append(material->colorHigh);
                append(material->response);
                append(material->ramp);
                append(material->pattern);
            }
            append(surface->blend);
            append(surface->detail);
        }
        append(biome.slope);
        return hash;
    }

    inline std::size_t upgradeTerrainAppearance(
        ProceduralTerrain::TerrainAppearance& appearance, std::uint32_t& revision)
    {
        if (revision >= TerrainAppearanceRevision
            || !ProceduralTerrain::validTerrainAppearance(appearance)) return 0;

        // Original authored presets from 426d806, in biome-ID order. Keeping
        // compact fingerprints avoids a second, obsolete material table.
        constexpr std::uint64_t legacy[] = {
            0xb5416b9f6300dd23ull, 0x0ab4c1fdd84a29adull,
            0x528722caa137aa5dull, 0xbc8d98b25f4182c8ull,
            0xd48169bf95dffeecull, 0x375c0a2b570e8334ull,
            0x8c1cd46cb2b0966cull, 0xd15e562074a99350ull,
            0xb42eb9ce611b94cbull, 0xe02f231023cd2adeull,
            0x508388eed365b7c0ull, 0x203fb399acf82b4aull,
            0xdc3ce0c5ca6e3e81ull, 0xc3a57bcf7b2b64e6ull,
            0x9ee96bf96636ce89ull, 0xa79148ade3e1f279ull,
            0xfda1ac2d992a5f9cull, 0xbc2933f7dbcf8da4ull,
            0x2925ff46395f4884ull, 0xeec63fd336a738f6ull,
            0x468f702f3fed6c98ull, 0x6ba7efe17e1acfc0ull,
            0x4ff6e34cbb81309eull, 0x2c595083c8133152ull,
            0x33231f3a9e336ee3ull, 0xd5b5ffb53331bd1eull
        };
        static_assert(sizeof(legacy) / sizeof(*legacy) == ProceduralTerrain::BiomeCount,
            "Every v1 biome needs its original fingerprint");
        const auto defaults = makeTerrainAppearance();
        std::size_t upgraded = 0;
        // Only v1 factory palette records need replacement. Later revisions
        // keep their palettes, including artist-edited blue ice.
        for (std::size_t i = 0; revision < 2 && i < ProceduralTerrain::BiomeCount; ++i)
        {
            if (terrainAppearanceLegacyFingerprint(appearance.biomes[i]) == legacy[i])
            {
                appearance.biomes[i] = defaults.biomes[i];
                ++upgraded;
            }
        }
        // v3 saved the former ground-only selection. Upgrade only unchanged
        // factory fine controls; retain explicit artist edits and all palettes.
        // v1/v2 had no fine controls, so classify their actual loaded colors.
        auto classified = appearance;
        configureTerrainFineMaterials(classified);
        for (std::size_t i = 0; i < ProceduralTerrain::BiomeCount; ++i)
        {
            auto& b = appearance.biomes[i];
            const auto& fresh = classified.biomes[i];
            b.stainRouting = fresh.stainRouting;
            ProceduralTerrain::TerrainMaterial* materials[] = {
                &b.ground.base, &b.ground.accent, &b.wall.base, &b.wall.accent};
            const ProceduralTerrain::TerrainMaterial* replacements[] = {
                &fresh.ground.base, &fresh.ground.accent, &fresh.wall.base, &fresh.wall.accent};
            for (unsigned slot = 0; slot < 4; ++slot)
            {
                ProceduralTerrain::TerrainMaterialVector old{{0.f,.16f,0.f,0.f}};
                if (slot < 2)
                    for (unsigned soil : {2u,3u,4u,7u,8u,9u,11u,12u,13u,14u,16u,17u,18u,19u,23u,24u})
                        if (i == soil) old = {{0.f,.16f,.10f,.06f}};
                if (slot == 0)
                {
                    if (i == 0) old = {{.90f,.15f,0.f,0.f}};
                    if (i == 1) old = {{.55f,.18f,0.f,0.f}};
                    if (i == 15) old = {{.60f,.20f,0.f,0.f}};
                    if (i == 21) old = {{.85f,.17f,0.f,0.f}};
                    if (i == 25) old = {{.65f,.18f,0.f,0.f}};
                }
                bool unchanged = true;
                for (unsigned c = 0; c < 4; ++c)
                    unchanged &= std::abs(materials[slot]->fineSurface[c]-old[c]) < 0.000001f;
                if (revision < 3 || unchanged)
                    materials[slot]->fineSurface = replacements[slot]->fineSurface;
            }
        }
        revision = TerrainAppearanceRevision;
        return upgraded;
    }
}
