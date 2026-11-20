#pragma once

#include "EnduranceTerrainMaterials.hpp"
#include <cmath>
#include <cstdint>

namespace Endurance
{
    // Scene data version, deliberately outside the renderer's std140 records.
    constexpr std::uint32_t TerrainAppearanceRevision = 2;

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
        for (std::size_t i = 0; i < ProceduralTerrain::BiomeCount; ++i)
        {
            if (terrainAppearanceLegacyFingerprint(appearance.biomes[i]) == legacy[i])
            {
                appearance.biomes[i] = defaults.biomes[i];
                ++upgraded;
            }
        }
        revision = TerrainAppearanceRevision;
        return upgraded;
    }
}
