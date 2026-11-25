// Standalone test binary only; the engine keeps its single STB implementation
// in TextureManager.cpp. Exercise the actual game's loader and process cache.
#define STB_IMAGE_IMPLEMENTATION
#include "stb-cmake/stb_image.h"
#include "EnduranceTerrainGrain.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    try
    {
        const auto image = Endurance::terrainGrainImage();
        if (!image || !ProceduralTerrain::validTerrainGrainImage(*image)
            || image->width != 1024 || image->height != 1024
            || image != Endurance::terrainGrainImage())
            throw std::runtime_error("Grain asset dimensions, packing or process cache is invalid");
        bool failed = false;
        try { Endurance::loadTerrainGrainImage("missing-terrain-grain.png", "missing-normal.png"); }
        catch (const std::runtime_error&) { failed = true; }
        if (!failed) throw std::runtime_error("Missing grain pair silently accepted");
        if (argc > 1 && std::string(argv[1]) == "--dump")
            std::cout.write(reinterpret_cast<const char*>(image->rgba.data()), image->rgba.size());
        else
            std::cout << "PASS production grain PNG loader, matching dimensions, RGBA packing, cache and missing-file failure\n";
        return std::cout.good() ? 0 : 1;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
