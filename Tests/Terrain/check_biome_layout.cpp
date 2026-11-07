// Export and independently verify the production CPU biome layout. GPU tests
// consume this table instead of maintaining a second copy of the matrices.
#include "ProceduralTerrainBiomes.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ProceduralTerrain;
namespace {
void require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }
void check()
{
    constexpr auto cells=makeBiomeCellTable();
    constexpr auto masks=makeBiomeCellMasks();
    require(sizeof(cells)==1024 && FieldColumnBytes==48, "Unexpected GPU layout size");
    for (unsigned cell=0;cell<64;++cell)
    {
        const unsigned x=cell%4, y=(cell/4)%4, z=cell/16;
        unsigned surface=0, volume=0;
        for (unsigned c=0;c<8;++c)
        {
            const unsigned dx=c&1,dy=(c>>1)&1,dz=c>>2;
            const auto id=(cells[cell][c<6?1:2]>>(5*(c<6?c:c-6)))&31u;
            require(id==BiomeMatrix3D[x+dx+5*(y+dy)+25*(z+dz)], "Packed volume corner differs from CPU matrix");
            volume|=1u<<id;
            if(c<4)
            {
                const auto id2=(cells[cell][0]>>(5*c))&31u;
                require(id2==BiomeMatrix2D[x+dx+5*(y+dy)], "Packed surface corner differs from CPU matrix");
                surface|=1u<<id2;
            }
        }
        if(z==0)surface|=1u<<MarineBiome;
        require(masks[cell].surface==surface && masks[cell].volume==volume, "Cell mask misses a recipe");
        const auto selected=biomeMasksForCells(cell<32?1u<<cell:0,cell>=32?1u<<(cell-32):0);
        require(selected.key()==masks[cell].key(), "Mask reduction changes cell selection");
        // Includes endpoints and interiors: duplicate corner IDs must add their
        // contributions; they must not cause repeated evaluation or lose mass.
        for(double fx:{0.,.17,.5,1.})for(double fy:{0.,.43,1.})for(double fz:{0.,.73,1.})
        {
            double weights[BiomeCount]{};
            for(unsigned c=0;c<8;++c)
            {
                const auto id=(cells[cell][c<6?1:2]>>(5*(c<6?c:c-6)))&31u;
                weights[id]+=((c&1)?fx:1-fx)*((c&2)?fy:1-fy)*((c&4)?fz:1-fz);
            }
            double sum=0;for(double weight:weights){require(weight>=0 && weight<=1.0000001,"Invalid local weight");sum+=weight;}
            require(std::abs(sum-1)<1e-12,"Local trilinear weights do not sum to one");
            if(z==0 && fz==0)require(weights[MarineBiome]==1,"Lowest volume slice is not exclusively marine");
        }
    }
    const auto all=biomeMasksForCells(~0u,~0u);
    require(all.surface==AllBiomes && all.volume==AllBiomes,"Complete matrix does not cover every biome");
    require(biomeMasksForCells(0,0).key()==0,"Empty cell mask enabled biomes");
    require(BiomeMasks{1,2}.key()!=BiomeMasks{2,1}.key(),"Surface/volume variant keys alias");
    const auto defines=biomeDefines({1u,1u<<MarineBiome});
    require(defines.find("BIOME_SURFACE_POLAR_ICE")!=std::string::npos &&
            defines.find("BIOME_VOLUME_MARINE")!=std::string::npos &&
            defines.find("BIOME_VOLUME_POLAR_ICE")==std::string::npos,"Variant defines ignore masks");
}
}
int main(int argc,char** argv)
{
    try
    {
        check();
        if(argc==2 && std::string(argv[1])=="--cells")
        {
            const auto table=makeBiomeCellTable();
            return std::fwrite(table.data(),1,sizeof(table),stdout)==sizeof(table)?0:1;
        }
        if(argc==4 && std::string(argv[1])=="--defines")
        {
            std::cout<<biomeDefines({static_cast<std::uint32_t>(std::stoul(argv[2])),static_cast<std::uint32_t>(std::stoul(argv[3]))});return 0;
        }
        if(argc==4 && std::string(argv[1])=="--mask")
        {
            const auto masks=biomeMasksForCells(static_cast<std::uint32_t>(std::stoul(argv[2])),static_cast<std::uint32_t>(std::stoul(argv[3])));
            std::cout<<masks.surface<<' '<<masks.volume;return 0;
        }
        std::cout<<"PASS biome CPU layout: 64 packed cells, conservative masks, local weight normalization, marine slice and canonical variant keys\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL biome layout: "<<error.what()<<'\n';return 1;}
}
