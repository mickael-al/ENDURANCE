#include "EnduranceTerrainGrain.hpp"
#include "stb-cmake/stb_image.h" // Implementation belongs to TextureManager.cpp.
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Endurance
{
    std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> loadTerrainGrainImage(
        const std::string& noisePath, const std::string& normalPath)
    {
        int width = 0, height = 0, normalWidth = 0, normalHeight = 0, channels = 0;
        if (!stbi_info(noisePath.c_str(), &width, &height, &channels)
            || !stbi_info(normalPath.c_str(), &normalWidth, &normalHeight, &channels))
            throw std::runtime_error("Cannot read terrain grain images: " + noisePath + " / " + normalPath);
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096
            || width != normalWidth || height != normalHeight)
            throw std::runtime_error("Terrain grain images must have matching dimensions, up to 4096x4096.");

        using Pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
        Pixels noise(stbi_load(noisePath.c_str(), &width, &height, &channels, 1), stbi_image_free);
        Pixels normal(stbi_load(normalPath.c_str(), &normalWidth, &normalHeight, &channels, 3), stbi_image_free);
        if (!noise || !normal || width != normalWidth || height != normalHeight
            || width <= 0 || height <= 0 || width > 4096 || height > 4096)
            throw std::runtime_error("Cannot decode matching terrain grain images.");

        auto image = std::make_shared<ProceduralTerrain::TerrainGrainImage>();
        image->width = static_cast<unsigned>(width);
        image->height = static_cast<unsigned>(height);
        const std::size_t pixels = std::size_t(width) * height;
        image->rgba.resize(pixels * 4);
        for (std::size_t i = 0; i < pixels; ++i)
        {
            image->rgba[4*i] = image->rgba[4*i+1] = 128;
            image->rgba[4*i+2] = image->rgba[4*i+3] = 0;
        }

        // Isolated, softly edged facets, with ONE orientation per grain. A
        // continuous Sobel normal map selected by N.H produces moving contour
        // lines instead. The original images still supply the deterministic
        // distribution and orientations; this bake never runs per chunk/frame.
        constexpr int cellSize = 8;
        constexpr float slopeRange = 0.6f; // Shared with fine_surface.glsl.
        const auto byte = [](float v) {
            return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0l, 255l));
        };
        const auto random = [](std::uint32_t value) { return float(value >> 8) / 16777216.f; };
        const auto wrap = [](int v, int period) { return (v % period + period) % period; };
        for (int cy = 0; cy < height; cy += cellSize)
        for (int cx = 0; cx < width; cx += cellSize)
        {
            const std::size_t source = std::size_t(cy) * width + cx;
            const auto hash = [&](unsigned salt) {
                return ProceduralTerrain::FineSurfaceBake::hash(cx, cy,
                    salt ^ (unsigned(noise.get()[source]) * 0x9e3779b9u));
            };
            if (random(hash(0x9fe17a23u)) > 0.6f) continue;
            const float x = cx + 4.f + (random(hash(0x37e84ab1u)) - .5f) * 4.f;
            const float y = cy + 4.f + (random(hash(0x1b73c09du)) - .5f) * 4.f;
            const float radius = 1.8f + random(hash(0xc40af39bu));
            const float nx = normal.get()[3*source] * (2.f/255.f) - 1.f;
            const float ny = normal.get()[3*source+1] * (2.f/255.f) - 1.f;
            const float nz = normal.get()[3*source+2] * (1.f/255.f); // B is positive Z.
            // The source Sobel normals mostly lie on a ring of steep slopes.
            // Keep their azimuth, but distribute slopes around the surface
            // normal so the reflection fills its centre, not just two edges.
            const float tilt = (std::min)(.45f, .14f * std::sqrt(-2.f * std::log(
                (std::max)(random(hash(0x6e32d091u)), 1.e-6f)))) * (1.f-.25f*nz);
            const float divisor = (std::max)(std::sqrt(nx*nx+ny*ny), 1.e-6f);
            const float sx = tilt * nx / divisor, sy = tilt * ny / divisor;
            for (int py = int(std::floor(y-radius)); py <= int(std::ceil(y+radius)); ++py)
            for (int px = int(std::floor(x-radius)); px <= int(std::ceil(x+radius)); ++px)
            {
                float coverage = 0.f;
                // Integrate the compact edge before storing it. Bilinear/mip
                // filtering then operates on coverage, not a thresholded noise.
                for (int iy = 0; iy < 2; ++iy)
                for (int ix = 0; ix < 2; ++ix)
                {
                    const float dx = px + .25f + .5f*ix - x;
                    const float dy = py + .25f + .5f*iy - y;
                    const float t = std::clamp((radius-std::sqrt(dx*dx+dy*dy))/.5f, 0.f, 1.f);
                    coverage += .25f * t*t*(3.f-2.f*t);
                }
                const std::size_t target = 4 * (std::size_t(wrap(py,height))*width + wrap(px,width));
                if (byte(coverage*255.f) <= image->rgba[target+3]) continue;
                // Linear slope moments, including the flat substrate. Hardware
                // mip generation preserves BOTH the mean and second moment;
                // normalizing before averaging would discard subpixel variance.
                image->rgba[target] = byte(128.f + coverage*sx*(127.f/slopeRange));
                image->rgba[target+1] = byte(128.f + coverage*sy*(127.f/slopeRange));
                image->rgba[target+2] = byte(coverage*(sx*sx+sy*sy)*(255.f/(slopeRange*slopeRange)));
                image->rgba[target+3] = byte(coverage*255.f);
            }
        }
        return image;
    }

    std::shared_ptr<const ProceduralTerrain::TerrainGrainImage> terrainGrainImage()
    {
        static const auto image = loadTerrainGrainImage(
            "../ENDURANCE/Ressources/Texture/Terrain/grain.png",
            "../ENDURANCE/Ressources/Texture/Terrain/NormalMap.png");
        return image;
    }
}
