// Standalone test binary only; the engine keeps its single STB implementation
// in TextureManager.cpp. Exercise the actual game's loader and process cache.
#define STB_IMAGE_IMPLEMENTATION
#include "stb-cmake/stb_image.h"
#include "EnduranceTerrainGrain.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
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
        // Check the produced material, not a copy of the bake algorithm:
        // sparse coverage, bounded finite moments and compact disconnected
        // grain silhouettes. This rejects the old percolating noise contours.
        std::size_t covered = 0, core = 0, compact = 0, components = 0, largest = 0;
        const std::size_t count = std::size_t(image->width)*image->height;
        std::vector<bool> seen(count);
        std::vector<std::size_t> queue;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto* p = &image->rgba[4*i];
            if (!p[3] && (p[0]!=128 || p[1]!=128 || p[2]!=0))
                throw std::runtime_error("Uncovered substrate has nonzero slope moments");
            covered += p[3]>127; core += p[3]==255;
            if (p[3]==255)
            {
                const float sx=(int(p[0])-128)*(.6f/127.f), sy=(int(p[1])-128)*(.6f/127.f);
                if (sx*sx+sy*sy>.21f || std::abs(p[2]*(.36f/255.f)-sx*sx-sy*sy)>.004f)
                    throw std::runtime_error("Resolved facet moments are inconsistent");
            }
            if (p[3]<=127 || seen[i]) continue;
            queue.clear(); queue.push_back(i); seen[i]=true;
            unsigned xmin=image->width, xmax=0, ymin=image->height, ymax=0;
            for (std::size_t q = 0; q < queue.size(); ++q)
            {
                const unsigned x=queue[q]%image->width, y=queue[q]/image->width;
                xmin=(std::min)(xmin,x); xmax=(std::max)(xmax,x);
                ymin=(std::min)(ymin,y); ymax=(std::max)(ymax,y);
                for (int dy=-1; dy<=1; ++dy)
                for (int dx=-1; dx<=1; ++dx)
                {
                    const int nx=int(x)+dx, ny=int(y)+dy;
                    if (nx<0 || ny<0 || nx>=int(image->width) || ny>=int(image->height)) continue;
                    const std::size_t j=std::size_t(ny)*image->width+nx;
                    if (!seen[j] && image->rgba[4*j+3]>127) { seen[j]=true; queue.push_back(j); }
                }
            }
            ++components; largest=(std::max)(largest,queue.size());
            const float aspect=float(xmax-xmin+1)/float(ymax-ymin+1);
            compact += aspect>.4f && aspect<2.5f;
        }
        if (covered<count*.05 || covered>count*.3 || core<count*.02
            || components<1000 || compact<components*.95 || largest>500)
            throw std::runtime_error("Grain coverage is not sparse and compact");
        if (argc > 1 && std::string(argv[1]) == "--dump")
            std::cout.write(reinterpret_cast<const char*>(image->rgba.data()), image->rgba.size());
        else
            std::cout << "PASS grain PNG loader/bake: compact facets, linear moments, cache and missing-file failure\n";
        return std::cout.good() ? 0 : 1;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
