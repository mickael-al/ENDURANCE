// CPU-only selection benchmark. The Python driver compares complete outputs,
// not just leaf counts, against a supplied pre-optimization source revision.
#include "ProceduralTerrainLod.hpp"
#include "ProceduralTerrainField.hpp"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    using namespace ProceduralTerrain;
    try
    {
        if (argc != 3) throw std::runtime_error("Expected iterations and snapshot path");
        const unsigned iterations = static_cast<unsigned>(std::stoul(argv[1]));
        if (!iterations || iterations > 10000) throw std::runtime_error("Invalid iteration count");
        const glm::vec3 cameras[] = {{80,180,120},{0,0,0},{6000,200,-1000},
            {-8000,600,17000},{800,2500,-600},{-500,5000,700}};
        std::vector<std::uint32_t> snapshot;
        std::uint64_t leaves = 0;
        double milliseconds = 0.0;
        for (unsigned repeat = 0; repeat < iterations; ++repeat)
            for (unsigned large = 0; large < 2; ++large)
                for (const auto& camera : cameras)
                {
                    LodConfig config;
                    FieldParameters field;
                    field.heightAmplitude = large ? 1000.0f : 320.0f;
                    const auto bounds = fieldHeightBounds(field);
                    config.minimumY = bounds.minimumY - 64.0f;
                    config.maximumY = bounds.maximumY + 64.0f;
                    config.viewDistance = large ? 40096.0f : 4096.0f;
                    config.fullResolutionRadius = large ? 0.0f : 128.0f;
                    config.maxLeaves = large ? 3072 : 2048;
                    std::vector<LodChunk> chunks;
                    std::string error;
                    const auto start = std::chrono::steady_clock::now();
                    const bool success = selectLodChunks(camera, config, chunks, error);
                    milliseconds += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
                    if (!success) throw std::runtime_error(error);
                    leaves += chunks.size();
                    // Include every repeat, the ordered complete result and
                    // masks, with case lengths to preserve boundaries.
                    snapshot.push_back(static_cast<std::uint32_t>(chunks.size()));
                    for (const auto& chunk : chunks)
                    {
                        for (auto origin : chunk.origin)
                            snapshot.push_back(static_cast<std::uint32_t>(origin));
                        snapshot.push_back(chunk.lod);
                        snapshot.push_back(chunk.transitionMask);
                    }
                }
        std::ofstream file(argv[2], std::ios::binary);
        file.write(reinterpret_cast<const char*>(snapshot.data()),
            static_cast<std::streamsize>(snapshot.size() * sizeof(std::uint32_t)));
        if (!file) throw std::runtime_error("Cannot write selection snapshot");
        std::cout << std::setprecision(12) << "{\"milliseconds\":" << milliseconds
            << ",\"selections\":" << 12 * iterations << ",\"leaves\":" << leaves << "}\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
