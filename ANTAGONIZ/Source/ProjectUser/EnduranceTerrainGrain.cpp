#include "EnduranceTerrainGrain.hpp"
#include "stb-cmake/stb_image.h" // Implementation belongs to TextureManager.cpp.
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
            image->rgba[4*i] = normal.get()[3*i];
            image->rgba[4*i+1] = normal.get()[3*i+1];
            image->rgba[4*i+2] = normal.get()[3*i+2];
            image->rgba[4*i+3] = noise.get()[i];
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
