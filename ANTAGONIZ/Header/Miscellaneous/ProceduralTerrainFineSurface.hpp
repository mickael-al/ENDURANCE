#pragma once

#include <array>
#include <cstdint>

namespace ProceduralTerrain
{
    inline constexpr unsigned TerrainFineSurfaceSize = 512;
    inline constexpr unsigned TerrainFineSurfaceMipLevels = 10;
    using TerrainFineSurfacePixels = std::array<std::uint8_t, TerrainFineSurfaceSize * TerrainFineSurfaceSize>;
    namespace FineSurfaceBake
    {
        inline std::uint32_t hash(unsigned x, unsigned y, unsigned salt) noexcept
        {
            std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ salt;
            h ^= h >> 16; h *= 0x7feb352du;
            h ^= h >> 15; h *= 0x846ca68bu;
            return h ^ (h >> 16);
        }

        // Periodic quintic value noise; power-of-two lattice wraps are exact.
        inline float noise(float x, float y, unsigned cells, unsigned salt) noexcept
        {
            x *= cells; y *= cells;
            const unsigned ix = static_cast<unsigned>(x), iy = static_cast<unsigned>(y);
            const float fx = x - ix, fy = y - iy;
            const float u = fx*fx*fx*(fx*(fx*6.f-15.f)+10.f);
            const float v = fy*fy*fy*(fy*(fy*6.f-15.f)+10.f);
            const auto at = [=](unsigned px, unsigned py) {
                return static_cast<float>(hash(px & (cells-1), py & (cells-1), salt) >> 8)
                    * (1.f / 16777216.f);
            };
            const float a = at(ix,iy), b = at(ix+1,iy);
            const float c = at(ix,iy+1), d = at(ix+1,iy+1);
            return (a+(b-a)*u)*(1.f-v)+(c+(d-c)*u)*v;
        }

        inline bool stain(float u, float v) noexcept
        {
            float sum = 0.f, amplitude = 1.f, normalization = 0.f;
            unsigned cells = 64;
            for (unsigned octave = 0; octave < 8; ++octave)
            {
                sum += amplitude * noise(u,v,cells,0x49a38b1du + octave*0x9e3779b9u);
                normalization += amplitude;
                amplitude *= 0.5f;
                cells *= 2;
            }
            return sum > 0.5f * normalization;
        }
    }

    inline const TerrainFineSurfacePixels& terrainFineSurfacePixels()
    {
        // Lazy, thread-safe, once per process, independent of chunk generation.
        // Threshold BEFORE filtering. Mipmaps average coverage rather than
        // thresholding a blurred noise (which would flicker/disappear at range).
        static const TerrainFineSurfacePixels pixels = [] {
            TerrainFineSurfacePixels result{};
            for (unsigned y = 0; y < TerrainFineSurfaceSize; ++y)
            for (unsigned x = 0; x < TerrainFineSurfaceSize; ++x)
            {
                unsigned coverage = 0;
                for (unsigned sy = 0; sy < 2; ++sy)
                for (unsigned sx = 0; sx < 2; ++sx)
                    coverage += FineSurfaceBake::stain(
                        (x + 0.25f + 0.5f*sx) / TerrainFineSurfaceSize,
                        (y + 0.25f + 0.5f*sy) / TerrainFineSurfaceSize) ? 1u : 0u;
                result[x + y*TerrainFineSurfaceSize] = static_cast<std::uint8_t>((coverage*255u+2u)/4u);
            }
            return result;
        }();
        return pixels;
    }
}
