#include "ProceduralMoonExport.hpp"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
long long integer(const std::string& text)
{
    std::size_t end=0; const auto value=std::stoll(text,&end);
    if(end!=text.size()) throw std::runtime_error("Invalid integer: "+text);
    return value;
}
int smallInteger(const std::string& text)
{
    const auto value=integer(text);
    if(value<(std::numeric_limits<int>::min)() || value>(std::numeric_limits<int>::max)())
        throw std::runtime_error("Integer outside supported range.");
    return static_cast<int>(value);
}
float number(const std::string& text)
{
    std::size_t end=0; const float value=std::stof(text,&end);
    std::uint32_t bits=0; std::memcpy(&bits,&value,4);
    if(end!=text.size() || (bits&0x7f800000u)==0x7f800000u) throw std::runtime_error("Expected finite number: "+text);
    return value;
}
void help()
{
    std::cout<<"Native ENDURANCE excavated Moon mesh + embedded texture (no graphics context).\n"
        "Usage: endurance_moon_model [--output moon.glb] [options]\n"
        "  --seed N             Deterministic shape/material seed\n"
        "  --resolution N       Geometry cells across Moon, 32..256 (default 128)\n"
        "  --width N            Material atlas width, 64..4096 (default 2048)\n"
        "  --excavation X       Excavation strength, zero gives intact Moon\n"
        "  --cavity-count N     Number of excavation volumes\n"
        "  --cavity-size X      Width of main excavation relative to Moon radius\n"
        "  --cavity-depth X     Excavation penetration\n"
        "  --yaw X --pitch X    Excavation direction in degrees\n"
        "  --erosion X          Geometric rock irregularity\n"
        "  --erosion-scale X    Geometric noise frequency\n"
        "  --octaves N          Geometric noise octaves\n"
        "  --debris-count N     Detached fragment count\n"
        "  --debris-spread X    Fragment displacement\n"
        "  --debris-size X      Fragment scale\n"
        "  --macro-scale X      Material maria frequency\n"
        "  --detail-scale X     Material detail frequency\n"
        "Parameters are bounded by the same sanitizer as the editor.\n"
        "Output is .glb or embedded .gltf; normalized radius one.\n"
        "No files are written without --output. Original moon.surface is untouched.\n";
}
}
int main(int argc,char** argv)
{
    try
    {
        EnduranceMoon::GeometryParams geometry;
        EnduranceMoon::Params material;
        material.fractureCount=0; material.fractureStrength=0;
        std::string output;
        for(int i=1;i<argc;++i)
        {
            const std::string option=argv[i];
            if(option=="--help" || option=="-h") { help(); return 0; }
            if(i+1>=argc) throw std::runtime_error("Missing value for "+option);
            const std::string value=argv[++i];
            if(option=="--output") output=value;
            else if(option=="--seed")
            {
                const auto seed=integer(value);
                if(seed<0 || seed>4294967295ll) throw std::runtime_error("Seed must fit uint32.");
                geometry.seed=material.seed=static_cast<std::uint32_t>(seed);
            }
            else if(option=="--resolution") geometry.resolution=smallInteger(value);
            else if(option=="--width") material.width=smallInteger(value);
            else if(option=="--excavation") geometry.excavationStrength=number(value);
            else if(option=="--cavity-count") geometry.cavityCount=smallInteger(value);
            else if(option=="--cavity-size") geometry.cavitySize=number(value);
            else if(option=="--cavity-depth") geometry.cavityDepth=number(value);
            else if(option=="--yaw") geometry.excavationYaw=number(value);
            else if(option=="--pitch") geometry.excavationPitch=number(value);
            else if(option=="--erosion") geometry.erosionStrength=number(value);
            else if(option=="--erosion-scale") geometry.erosionScale=number(value);
            else if(option=="--octaves") geometry.erosionOctaves=smallInteger(value);
            else if(option=="--debris-count") geometry.debrisCount=smallInteger(value);
            else if(option=="--debris-spread") geometry.debrisSpread=number(value);
            else if(option=="--debris-size") geometry.debrisSize=number(value);
            else if(option=="--macro-scale") material.macroScale=number(value);
            else if(option=="--detail-scale") material.detailScale=number(value);
            else throw std::runtime_error("Unknown option: "+option);
        }
        if(output.empty()) { help(); return 2; }
        geometry=EnduranceMoon::sanitizeGeometry(geometry); material=EnduranceMoon::sanitize(material);
        const auto start=std::chrono::steady_clock::now();
        const auto mesh=EnduranceMoon::generateGeometry(geometry);
        const auto surface=EnduranceMoon::generate(material);
        std::string error;
        if(!EnduranceMoon::saveModel(mesh,surface,output,error,&geometry,&material)) throw std::runtime_error(error);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<output<<": "<<mesh.vertices.size()<<" vertices, "<<mesh.indices.size()/3<<" triangles, "
            <<mesh.parts.size()<<" parts, "<<surface.width<<"x"<<surface.height<<" albedo, "
            <<std::fixed<<std::setprecision(2)<<seconds<<" s\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr<<"Moon model export failed: "<<error.what()<<'\n'; return 1; }
}
