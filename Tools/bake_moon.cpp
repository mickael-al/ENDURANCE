#include "ProceduralMoon.hpp"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
void help()
{
    std::cout <<
        "Bake ENDURANCE's cracked lunar atlas in native C++ (no graphics context).\n"
        "Usage: endurance_moon_bake [options]\n"
        "  --output PATH            EMOON001 file (default: ENDURANCE/Ressources/Atmosphere/moon.surface)\n"
        "  --width N                Power of two, 64..4096 (default: 2048)\n"
        "  --seed N                 Unsigned 32-bit seed (default: 68291)\n"
        "  --macro-scale X          Deposit scale, .25..12 (default: 2.4)\n"
        "  --detail-scale X         Fine-noise scale, 2..96 (default: 18)\n"
        "  --octaves N              Simplex fBm octaves, 1..10 (default: 8)\n"
        "  --fracture-count N       Major faults, 0..32 (default: 9)\n"
        "  --fracture-width X       Angular half-width, .001.. .06 (default: .018)\n"
        "  --fracture-depth X       Relief / sphere radius, 0.. .05 (default: .025)\n"
        "  --fracture-strength X    Fracture multiplier, 0..3 (default: 1.25)\n"
        "  --help                   Show this help\n"
        "Run from the repository root to use the default relative output path.\n"
        "RGB stores object-space normals; alpha stores linear albedo. No lighting is baked.\n";
}
long long integer(const std::string& text, const std::string& option)
{
    std::size_t used=0;
    const long long value=std::stoll(text,&used,10);
    if (used!=text.size()) throw std::runtime_error("Invalid integer for "+option);
    return value;
}
float number(const std::string& text, const std::string& option)
{
    std::size_t used=0;
    const float value=std::stof(text,&used);
    if (used!=text.size()) throw std::runtime_error("Invalid number for "+option);
    // Check representation explicitly: the engine also builds with fast math.
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    if ((bits&0x7f800000u)==0x7f800000u)
        throw std::runtime_error("A finite number is required for "+option);
    return value;
}
}

int main(int argc, char** argv)
{
    try
    {
        EnduranceMoon::Params params;
        std::string output="ENDURANCE/Ressources/Atmosphere/moon.surface";
        for (int i=1; i<argc; ++i)
        {
            const std::string option=argv[i];
            if (option=="--help" || option=="-h") { help(); return 0; }
            if (i+1>=argc) throw std::runtime_error("Missing value for "+option);
            const std::string value=argv[++i];
            if (option=="--output") output=value;
            else if (option=="--seed")
            {
                const long long seed=integer(value,option);
                if (seed<0 || seed>static_cast<long long>((std::numeric_limits<std::uint32_t>::max)()))
                    throw std::runtime_error("Seed must be in 0..4294967295.");
                params.seed=static_cast<std::uint32_t>(seed);
            }
            else if (option=="--width" || option=="--octaves" || option=="--fracture-count")
            {
                const long long n=integer(value,option);
                if (n<(std::numeric_limits<int>::min)() || n>(std::numeric_limits<int>::max)())
                    throw std::runtime_error("Integer out of range for "+option);
                if (option=="--width") params.width=static_cast<int>(n);
                else if (option=="--octaves") params.octaves=static_cast<int>(n);
                else params.fractureCount=static_cast<int>(n);
            }
            else if (option=="--macro-scale") params.macroScale=number(value,option);
            else if (option=="--detail-scale") params.detailScale=number(value,option);
            else if (option=="--fracture-width") params.fractureWidth=number(value,option);
            else if (option=="--fracture-depth") params.fractureDepth=number(value,option);
            else if (option=="--fracture-strength") params.fractureStrength=number(value,option);
            else throw std::runtime_error("Unknown option: "+option);
        }
        const EnduranceMoon::Params safe=EnduranceMoon::sanitize(params);
        if (safe.width!=params.width || safe.octaves!=params.octaves || safe.fractureCount!=params.fractureCount ||
            safe.macroScale!=params.macroScale || safe.detailScale!=params.detailScale ||
            safe.fractureWidth!=params.fractureWidth || safe.fractureDepth!=params.fractureDepth || safe.fractureStrength!=params.fractureStrength)
            throw std::runtime_error("A parameter is outside its supported range; see --help.");
        const auto start=std::chrono::steady_clock::now();
        const EnduranceMoon::Surface surface=EnduranceMoon::generate(params);
        std::string error;
        if (!EnduranceMoon::save(surface,output,error)) throw std::runtime_error(error);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout << output << ": " << surface.width << 'x' << surface.height
                  << ", mean albedo " << std::fixed << std::setprecision(6) << surface.meanAlbedo
                  << ", " << std::setprecision(2) << seconds << " s\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Moon bake failed: " << error.what() << '\n';
        return 1;
    }
}
