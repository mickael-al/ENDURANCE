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
    require(sizeof(cells)==1024 && FieldColumnBytes==32 && FieldScratchColumnBytes==48,
            "Unexpected GPU storage/scratch layout size");
    bool surfaceSeen[BiomeCount]{}, volumeSeen[BiomeCount]{};
    surfaceSeen[MarineBiome]=true; // Surface marine relief is applied below sea level.
    bool duplicateCornersSeen=false;
    constexpr auto validMask=std::uint32_t{0xffffffffu}>>(32-BiomeCount);
    for (unsigned cell=0;cell<64;++cell)
    {
        const unsigned x=cell%4, y=(cell/4)%4, z=cell/16;
        bool present[BiomeCount]{};
        const auto mask=cells[cell][3];
        require(mask!=0 && (mask&~validMask)==0, "Volume mask contains undefined biome IDs");
        for (unsigned c=0;c<8;++c)
        {
            const unsigned dx=c&1,dy=(c>>1)&1,dz=c>>2;
            const auto id=(cells[cell][c<6?1:2]>>(5*(c<6?c:c-6)))&31u;
            require(id==BiomeMatrix3D[x+dx+5*(y+dy)+25*(z+dz)], "Packed volume corner differs from CPU matrix");
            require(id<BiomeCount, "Packed volume ID is out of range");
            duplicateCornersSeen|=present[id];
            present[id]=true;
            volumeSeen[id]=true;
            if(c<4)
            {
                const auto id2=(cells[cell][0]>>(5*c))&31u;
                require(id2==BiomeMatrix2D[x+dx+5*(y+dy)], "Packed surface corner differs from CPU matrix");
                require(id2<BiomeCount, "Packed surface ID is out of range");
                surfaceSeen[id2]=true;
            }
        }
        for(unsigned biome=0;biome<BiomeCount;++biome)
            require(((mask>>biome)&1u)==unsigned(present[biome]), "Volume mask does not exactly match distinct corners");
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
            double maskedSum=0;
            unsigned activeCount=0;
            for(unsigned biome=0;biome<BiomeCount;++biome)
                if(((mask>>biome)&1u)!=0 && weights[biome]>0)
                {
                    maskedSum+=weights[biome];
                    ++activeCount;
                }
            require(std::abs(maskedSum-1)<1e-12,"Mask traversal lost local weight");
            if(z==0 && fz==0)
                require(weights[MarineBiome]==1 && activeCount==1,
                        "Lowest volume slice must evaluate only marine despite land candidates in its mask");
        }
    }
    require(duplicateCornersSeen,"Layout fixture did not exercise duplicate volume IDs");
    for(unsigned biome=0;biome<BiomeCount;++biome)
        require(surfaceSeen[biome] && volumeSeen[biome], "Complete matrix does not cover every biome");
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
        std::cout<<"PASS biome CPU layout: 64 packed cells and exact candidate masks, duplicate IDs, local weights and marine-only slice\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL biome layout: "<<error.what()<<'\n';return 1;}
}
