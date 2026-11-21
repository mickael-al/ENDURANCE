#include "EnduranceObjectPlacement.hpp"
#include "ProceduralTerrainField.hpp"
#include "hv/cpputil/json.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Endurance
{
    bool loadObjectPlacement(const std::string& path, ProceduralTerrain::ObjectPlacementConfig& output,
        std::string& error)
    {
        using namespace ProceduralTerrain;
        namespace fs=std::filesystem;
        using Json=nlohmann::json;
        try
        {
            std::ifstream stream(path);
            if(!stream) throw std::runtime_error("Cannot open object placement catalogue: "+path);
            Json root; stream>>root;
            if(root.at("format")!="ENDURANCE.ObjectPlacement" || root.at("version")!=1)
                throw std::runtime_error("Unsupported object placement catalogue");
            ObjectPlacementConfig result;
            result.enabled=true;
            const auto integer=[](const Json& j,unsigned maximum)
            {
                if(!j.is_number_integer() || j.get<double>()<0 || j.get<double>()>maximum)
                    throw std::runtime_error("Object catalogue integer out of range");
                return j.get<std::uint32_t>();
            };
            const auto real=[](const Json& j)
            {
                if(!j.is_number()) throw std::runtime_error("Expected object catalogue number");
                const float value=j.get<float>();
                if(!finiteFieldValue(value)) throw std::runtime_error("Non-finite object catalogue number");
                return value;
            };
            result.maximumLod=integer(root.at("maximumLod"),6);
            result.maximumSectors=integer(root.at("maximumSectors"),65536);
            result.candidateSpacing=real(root.at("candidateSpacing"));
            result.minimumDistance=real(root.at("minimumDistance"));
            result.retentionDistance=real(root.at("retentionDistance"));
            if(!result.maximumSectors || result.candidateSpacing<=0 || result.minimumDistance<0 ||
                result.minimumDistance>result.candidateSpacing || result.retentionDistance<=0)
                throw std::runtime_error("Invalid object spacing, retention distance or sector budget");
            const auto& prototypes=root.at("prototypes");
            if(!prototypes.is_array() || prototypes.empty() || prototypes.size()>255)
                throw std::runtime_error("Object catalogue requires 1 to 255 prototypes");
            for(const auto& value:prototypes)
            {
                if(integer(value.at("id"),254)!=result.prototypes.size())
                    throw std::runtime_error("Object prototype IDs must be contiguous and ordered");
                ObjectPrototype p;
                const fs::path relative=value.at("asset").get<std::string>();
                if(relative.empty() || relative.is_absolute()) throw std::runtime_error("Expected relative object asset path");
                for(const auto& part:relative) if(part=="..") throw std::runtime_error("Object asset path escapes catalogue directory");
                const auto asset=(fs::path(path).parent_path()/relative).lexically_normal();
                if(!fs::exists(asset)) throw std::runtime_error("Missing object asset: "+asset.generic_string());
                p.asset=asset.generic_string(); p.capacity=integer(value.at("capacity"),1u<<24);
                p.boundingRadius=real(value.at("boundingRadius"));
                p.minimumScale=real(value.at("minimumScale")); p.maximumScale=real(value.at("maximumScale"));
                if(p.capacity<ObjectPageSize || p.capacity%ObjectPageSize || p.boundingRadius<0 ||
                    p.minimumScale<=0 || p.maximumScale<p.minimumScale)
                    throw std::runtime_error("Invalid object capacity, bounds or scale");
                result.prototypes.push_back(std::move(p));
            }
            const auto& groups=root.at("groups");
            if(!groups.is_array() || groups.size()>255) throw std::runtime_error("Invalid object groups");
            for(const auto& value:groups)
            {
                if(integer(value.at("id"),254)!=result.groups.size()) throw std::runtime_error("Object group IDs must be contiguous and ordered");
                const auto first=integer(value.at("first"),254), count=integer(value.at("count"),255);
                if(!count || first+count>result.prototypes.size()) throw std::runtime_error("Object group is outside prototype table");
                result.groups.push_back({first,count});
            }
            const auto& profiles=root.at("biomes");
            if(!profiles.is_array() || profiles.size()!=BiomeCount) throw std::runtime_error("Expected all 26 biome object profiles");
            for(std::size_t i=0;i<BiomeCount;++i)
            {
                const auto& value=profiles[i]; auto& p=result.biomes[i];
                if(integer(value.at("id"),BiomeCount-1)!=i) throw std::runtime_error("Biome object profiles must be ordered by stable biome ID");
                const auto& choices=value.at("choices");
                if(!choices.is_array() || choices.size()>4) throw std::runtime_error("At most four object groups per biome");
                p.groups=0xffffffffu; p.weights=0;
                for(std::size_t slot=0;slot<choices.size();++slot)
                {
                    const auto g=integer(choices[slot].at("group"),254), w=integer(choices[slot].at("weight"),255);
                    if(g>=result.groups.size()) throw std::runtime_error("Unknown object group");
                    p.groups=(p.groups&~(255u<<(8*slot)))|(g<<(8*slot)); p.weights|=w<<(8*slot);
                }
                p.density=real(value.at("density")); p.minimumNormalY=real(value.at("minimumNormalY"));
                p.forestPeriod=real(value.at("forestPeriod")); p.forestThreshold=real(value.at("forestThreshold"));
                p.forestSoftness=real(value.at("forestSoftness")); p.minimumAboveSea=real(value.at("minimumAboveSea"));
                if(p.density<0 || p.density>1 || p.minimumNormalY<=0 || p.minimumNormalY>1 ||
                    p.forestPeriod<=0 || p.forestSoftness<=0)
                    throw std::runtime_error("Invalid object biome density, slope or forest mask");
            }
            output=std::move(result); error.clear(); return true;
        }
        catch(const std::exception& e) { error=e.what(); return false; }
    }
}
