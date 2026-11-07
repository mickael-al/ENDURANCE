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
    require(sizeof(cells)==1024 && FieldColumnBytes==48, "Unexpected GPU layout size");
    bool surfaceSeen[BiomeCount]{}, volumeSeen[BiomeCount]{};
    surfaceSeen[MarineBiome]=true; // Surface marine relief is applied below sea level.
    for (unsigned cell=0;cell<64;++cell)
    {
        const unsigned x=cell%4, y=(cell/4)%4, z=cell/16;
        for (unsigned c=0;c<8;++c)
        {
            const unsigned dx=c&1,dy=(c>>1)&1,dz=c>>2;
            const auto id=(cells[cell][c<6?1:2]>>(5*(c<6?c:c-6)))&31u;
            require(id==BiomeMatrix3D[x+dx+5*(y+dy)+25*(z+dz)], "Packed volume corner differs from CPU matrix");
            volumeSeen[id]=true;
            if(c<4)
            {
                const auto id2=(cells[cell][0]>>(5*c))&31u;
                require(id2==BiomeMatrix2D[x+dx+5*(y+dy)], "Packed surface corner differs from CPU matrix");
                surfaceSeen[id2]=true;
            }
        }
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
        std::cout<<"PASS biome CPU layout: 64 packed cells, local weight normalization and marine slice\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL biome layout: "<<error.what()<<'\n';return 1;}
}
