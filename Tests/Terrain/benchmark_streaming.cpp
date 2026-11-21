// Counts the actual mesh work requested by the production transaction planner.
// No GPU is simulated: timings are CPU planning only, jobs are exact requests.
#include "ProceduralTerrainStreaming.hpp"
#include "ProceduralTerrainField.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

int main(int argc, char** argv)
{
    using namespace ProceduralTerrain;
    try
    {
        if (argc != 2) throw std::runtime_error("Expected final-coverage snapshot path");
        std::vector<std::uint32_t> snapshot;
        std::cout << "[";
        for (unsigned large = 0; large < 2; ++large)
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
            const glm::vec3 camera(80,180,120);
            std::vector<LodChunk> current, desired;
            std::string error;
            if (!selectLodChunks(camera,config,desired,error)) throw std::runtime_error(error);
            LodPlanningCache cache;
            std::set<std::pair<std::array<std::int32_t,3>,std::uint32_t>> generated;
            unsigned transactions=0, jobs=0, repeated=0, largest=0;
            double milliseconds=0, peak=0;
            for (; transactions<10000; ++transactions)
            {
                LodTransaction transaction;
                const auto start = std::chrono::steady_clock::now();
                if (!planLodTransaction(current,desired,camera,config,transaction,error,&cache))
                    throw std::runtime_error(error);
                const auto elapsed = std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                milliseconds += elapsed; peak = (std::max)(peak,elapsed);
                if (transaction.kind==LodWorkKind::None) break;
                largest = (std::max)(largest,static_cast<unsigned>(transaction.nextIndices.size()));
                if (largest>32) throw std::runtime_error("Planner exceeded bounded mesh work");
                for (auto i : transaction.nextIndices)
                {
                    const auto& chunk = transaction.next.at(i);
                    ++jobs;
                    if (!generated.emplace(chunk.origin,chunk.lod).second) ++repeated;
                }
                current = std::move(transaction.next);
            }
            if (transactions==10000) throw std::runtime_error("Planner failed to converge");
            // Publication ordering may change, but every final origin/LOD and
            // transition mask must equal the requested complete selection.
            const auto less = [](const LodChunk& a,const LodChunk& b)
            { return std::tie(a.origin,a.lod,a.transitionMask)<std::tie(b.origin,b.lod,b.transitionMask); };
            std::sort(current.begin(),current.end(),less);
            std::sort(desired.begin(),desired.end(),less);
            if (current.size()!=desired.size()) throw std::runtime_error("Incomplete final coverage");
            snapshot.push_back(static_cast<std::uint32_t>(current.size()));
            for (std::size_t i=0; i<current.size(); ++i)
            {
                const auto& a=current[i]; const auto& b=desired[i];
                if (a.origin!=b.origin || a.lod!=b.lod || a.transitionMask!=b.transitionMask)
                    throw std::runtime_error("Final coverage/masks changed");
                for (auto coordinate : a.origin) snapshot.push_back(static_cast<std::uint32_t>(coordinate));
                snapshot.push_back(a.lod); snapshot.push_back(a.transitionMask);
            }
            if (large) std::cout << ',';
            std::cout << "{\"view_distance_m\":" << config.viewDistance
                << ",\"selected_chunks\":" << desired.size()
                << ",\"transactions\":" << transactions << ",\"mesh_jobs\":" << jobs
                << ",\"repeated_geometry_jobs\":" << repeated << ",\"largest_batch\":" << largest
                << ",\"planning_ms\":" << milliseconds << ",\"peak_planning_ms\":" << peak << "}";
        }
        std::ofstream file(argv[1],std::ios::binary);
        file.write(reinterpret_cast<const char*>(snapshot.data()),
            static_cast<std::streamsize>(snapshot.size()*sizeof(snapshot.front())));
        if (!file) throw std::runtime_error("Cannot write final coverage snapshot");
        std::cout << "]\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
