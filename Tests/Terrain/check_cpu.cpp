#include "ProceduralTerrainField.hpp"
#include "ProceduralTerrainLod.hpp"
#include "ProceduralTransvoxelTables.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ProceduralTerrain;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static bool same(const std::vector<LodChunk>& a, const std::vector<LodChunk>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i=0;i<a.size();++i)
        if (a[i].origin!=b[i].origin || a[i].lod!=b[i].lod || a[i].transitionMask!=b[i].transitionMask) return false;
    return true;
}

static void fields()
{
    FieldParameters p;
    require(validFieldParameters(p), "Default field parameters rejected");
    auto b=fieldHeightBounds(p);
    require(b.minimumY==-797.5f && b.maximumY==479.0f, "Default conservative field bounds changed");
    p.bandOffset=-200;
    b=fieldHeightBounds(p);
    require(b.minimumY==-997.5f && b.maximumY==404.0f, "Negative offset bounds");
    p.bandOffset=200;
    b=fieldHeightBounds(p);
    require(b.minimumY==-722.5f && b.maximumY==679.0f, "Positive offset bounds");
    p=FieldParameters{};
    p.bandThickness=0;
    require(validFieldParameters(p), "Zero thickness must be supported");
    p.heightAmplitude=-1;
    require(!validFieldParameters(p), "Negative amplitude accepted");
    p=FieldParameters{};
    p.mode=static_cast<FieldMode>(3);
    require(!validFieldParameters(p), "Unknown field mode accepted");
    p=FieldParameters{};
    p.sphereCenter[0]=std::numeric_limits<float>::infinity();
    require(!validFieldParameters(p), "Infinity accepted under release flags");
    p=FieldParameters{};
    p.baseHeight=std::numeric_limits<float>::quiet_NaN();
    require(!validFieldParameters(p), "NaN accepted under release flags");
    p=FieldParameters{};
    require(validFieldDomain(p,1e6),"Reasonable world domain rejected");
    require(!validFieldDomain(p,std::numeric_limits<double>::quiet_NaN()),"NaN domain accepted under release flags");
    require(!validFieldDomain(p,std::numeric_limits<double>::infinity()),"Infinite domain accepted under release flags");
    require(!validFieldDomain(p,1e11),"Integer noise-lattice overflow accepted");
    p.climateFrequency=1e10f;
    require(!validFieldDomain(p,1),"Excessive climate frequency accepted");
    p=FieldParameters{};p.mode=FieldMode::Sphere;
    require(validFieldDomain(p,1e10),"Analytic sphere unnecessarily restricted to noise domain");
    require(!validFieldDomain(p,0x1p61),"Sphere distance overflow accepted");
    p=FieldParameters{};p.baseHeight=1e30f;
    require(!validFieldParameters(p),"Field intermediates may overflow float");
    std::cout << "PASS field parameters: bounds, zero amplitudes, NaN/Inf rejection\n";
}

static void tables()
{
    const auto table=Transvoxel::makePackedTables();
    const unsigned transitionBits[]={0,1,2,7,8,3,6,5,4,0,2,6,4};
    for (unsigned transition=0;transition<2;++transition)
        for (unsigned pattern=0;pattern<(transition?512u:256u);++pattern)
        {
            const unsigned base=transition?1280+pattern*10:pattern*5;
            const auto count=table[base];
            require(count%3==0 && count<=(transition?36u:15u),"Invalid Transvoxel case vertex count");
            for (unsigned i=0;i<count;++i)
            {
                const auto edge=(table[base+1+i/4]>>(8*(i%4)))&255u;
                const auto a=edge>>4,b=edge&15;
                require(a<b && b<(transition?13u:8u),"Invalid Transvoxel edge endpoint");
                const auto bitA=transition?transitionBits[a]:a;
                const auto bitB=transition?transitionBits[b]:b;
                require(((pattern>>bitA)&1u)!=((pattern>>bitB)&1u),"Transvoxel edge does not cross the isosurface");
                require(!transition || (a<9)==(b<9),"Transition edge mixes coarse and fine endpoints");
            }
        }
    std::cout << "PASS tables: all 256 regular and 512 transition cases have valid counts and crossing edges\n";
}

// Independent O(n²) geometry oracle: no reuse of selector's neighbour search.
static void selection(const glm::vec3& camera, const LodConfig& p)
{
    std::vector<LodChunk> chunks, repeated;
    std::string error;
    const bool selected=selectLodChunks(camera,p,chunks,error);
    require(selected, error.c_str());
    require(!chunks.empty(), "Empty selection near terrain");
    require(selectLodChunks(camera,p,repeated,error) && same(chunks,repeated), "Nondeterministic LOD selection");
    std::size_t transitions=0;
    for (std::size_t i=0;i<chunks.size();++i)
    {
        const auto& c=chunks[i];
        const std::int64_t width=std::int64_t(p.cells)<<c.lod;
        require(c.lod<=p.maxLod, "LOD above maximum");
        for (auto origin:c.origin) require(origin%width==0, "Chunk not globally aligned");
        if (chunkDistanceSquared(camera,c,p.cells,p.baseSpacing)<=p.fullResolutionRadius*p.fullResolutionRadius)
            require(c.lod==0, "Collision radius does not force finest LOD");
        unsigned expectedMask=0;
        for (std::size_t j=0;j<chunks.size();++j)
        {
            if (i==j) continue;
            const auto& other=chunks[j];
            const std::int64_t otherWidth=std::int64_t(p.cells)<<other.lod;
            std::int64_t overlap[3];
            for (unsigned a=0;a<3;++a)
                overlap[a]=std::min(std::int64_t(c.origin[a])+width,std::int64_t(other.origin[a])+otherWidth)-
                           std::max(std::int64_t(c.origin[a]),std::int64_t(other.origin[a]));
            require(!(overlap[0]>0 && overlap[1]>0 && overlap[2]>0), "Overlapping LOD leaves");
            for (unsigned a=0;a<3;++a)
                if (overlap[a]==0 && overlap[(a+1)%3]>0 && overlap[(a+2)%3]>0)
                {
                    require(std::abs(int(c.lod)-int(other.lod))<=1, "Face neighbour violates 2:1 balance");
                    if (other.lod==c.lod+1)
                        expectedMask|=1u<<(a*2+(std::int64_t(c.origin[a])+width==other.origin[a] ? 1:0));
                }
        }
        require(c.transitionMask==expectedMask, "Transition mask disagrees with geometric neighbours");
        transitions+=expectedMask!=0;
    }
    require(transitions>0, "Fixture did not exercise LOD transitions");
    // Sample cell centres throughout the required spherical, vertically clipped
    // region; each must be covered by exactly one leaf.
    for (int z=-44;z<=44;z+=4)
        for (int y=-44;y<=44;y+=4)
            for (int x=-44;x<=44;x+=4)
            {
                glm::vec3 point=camera+glm::vec3(x+.375f,y+.375f,z+.375f);
                if (point.y<p.minimumY || point.y>p.maximumY) continue;
                auto d=point-camera;
                if (double(d.x)*d.x+double(d.y)*d.y+double(d.z)*d.z>=p.viewDistance*p.viewDistance) continue;
                unsigned hits=0;
                for (const auto& c:chunks)
                {
                    const double width=(std::uint64_t(p.cells)<<c.lod)*double(p.baseSpacing);
                    bool contains=true;
                    for (unsigned a=0;a<3;++a)
                        contains &= point[a]>=c.origin[a]*double(p.baseSpacing) && point[a]<c.origin[a]*double(p.baseSpacing)+width;
                    hits+=contains;
                }
                require(hits==1, "Required region contains a missing or multiply covered point");
            }
    std::cout << "PASS LOD: " << chunks.size() << " disjoint leaves, " << transitions << " transition owners; coverage, 2:1 balance and collision radius\n";
}

static void lod()
{
    LodConfig p;
    p.cells=8;p.maxLod=3;p.viewDistance=44;p.minimumY=-24;p.maximumY=24;
    p.fullResolutionRadius=9;p.splitDistanceFactor=.55f;p.maxLeaves=2048;
    selection(glm::vec3(1.25f,3.5f,-2.75f),p);
    selection(glm::vec3(-65.25f,-4.5f,-66.75f),p);
    p.splitDistanceFactor=.08f;
    selection(glm::vec3(-.25f,-.25f,-.25f),p); // Forces the balancing pass.
    std::vector<LodChunk> output{{{{123,456,789}},7,21}}, before=output;
    std::string error;
    p.maxLeaves=1;
    require(!selectLodChunks(glm::vec3(0),p,output,error) && !error.empty(), "Leaf budget failure was not reported");
    require(same(output,before), "Budget failure published partial terrain");
    p.maxLeaves=2048;
    p.baseSpacing=std::numeric_limits<float>::quiet_NaN();
    require(!selectLodChunks(glm::vec3(0),p,output,error), "LOD accepted NaN under release flags");
    require(same(output,before), "Invalid parameters changed visible selection");
    std::cout << "PASS LOD budget: failure preserves complete previous output; invalid input rejected\n";
    p=LodConfig{};
    const auto bounds=fieldHeightBounds(FieldParameters{});
    const float coarseSpacing=p.baseSpacing*float(1u<<p.maxLod);
    p.minimumY=bounds.minimumY-coarseSpacing;
    p.maximumY=bounds.maximumY+coarseSpacing;
    const bool selected=selectLodChunks(glm::vec3(80,180,120),p,output,error);
    require(selected,error.c_str());
    std::cout << "PASS demo defaults: " << output.size() << "/" << p.maxLeaves << " selected leaves\n";
}

int main(int argc,char** argv)
{
    try
    {
        if (argc==3 && std::string(argv[1])=="--export-tables")
        {
            const auto tables=Transvoxel::makePackedTables();
            std::ofstream out(argv[2],std::ios::binary);
            out.write(reinterpret_cast<const char*>(tables.data()),tables.size()*sizeof(tables[0]));
            require(bool(out),"Could not write packed table fixture");
            return 0;
        }
        fields();
        tables();
        lod();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
