// Reuse the existing controlled GPU/engine backend and real scheduler worker.
// Only completions differ: a sea-surface chunk is EMPTY, yet carries its biome
// presence mask. No terrain geometry can accidentally stand in for sea coverage.
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "ProceduralTerrainSea.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <tuple>
#include <vector>

namespace Test {
std::uint32_t seaMask(const ProceduralTerrain::ChunkRequest&, const ProceduralTerrain::GpuConfig&);
bool seaEmpty(const ProceduralTerrain::ChunkRequest&, const ProceduralTerrain::GpuConfig&);
void injectSeaCompletion(std::vector<ProceduralTerrain::Completion>&);
void checkSea(const ProceduralGeneration&, const std::vector<ProceduralTerrain::SeaTile>&,
              std::uint64_t, const ProceduralTerrain::SeaTile*);
void checkInitialSea(const ProceduralGeneration&);
void checkStoppedSea(const ProceduralGeneration&);
}

#define TERRAIN_SEA_STREAMING
#define main terrainSchedulerFixtureMain
#include "../Terrain/check_scheduler.cpp"
#undef main

namespace Test {
namespace {
using TileId=std::tuple<std::int32_t,std::int32_t,std::uint32_t>;
std::uint64_t checkedFrames=0, checkedPublications=0, emptyCompletions=0;
bool sawLand=false, sawMixedLod=false, injectStale=false, staleInjected=false;
std::set<TileId> tileIds(const std::vector<ProceduralTerrain::SeaTile>& tiles)
{
    std::set<TileId> result;
    for(const auto& tile:tiles) {
        require(tile.padding==0,"sea tile padding not deterministic");
        require(result.emplace(tile.baseX,tile.baseZ,tile.lod).second,"duplicate sea tile");
    }
    return result;
}
}

std::uint32_t seaMask(const ProceduralTerrain::ChunkRequest& chunk,
                      const ProceduralTerrain::GpuConfig& config)
{
    if(config.field.mode!=ProceduralTerrain::FieldMode::Terrain) return 0;
    if(config.field.biomeOverride>=0) return 1u<<unsigned(config.field.biomeOverride);
    // Positive X/Z quadrant is dry. Other chunks have a MIXED surface mask,
    // proving that a positive marine contribution need not be the sole biome.
    return (1u<<2u) | ((chunk.origin[0]<0 || chunk.origin[2]<0)
        ? (1u<<ProceduralTerrain::MarineBiome) : 0u);
}

bool seaEmpty(const ProceduralTerrain::ChunkRequest& chunk,
              const ProceduralTerrain::GpuConfig& config)
{
    const auto span=std::int64_t(config.cells)<<chunk.lod;
    const double lower=double(chunk.origin[1])*config.baseSpacing;
    const double upper=double(std::int64_t(chunk.origin[1])+span)*config.baseSpacing;
    const bool empty=lower<=config.field.seaLevel && config.field.seaLevel<upper;
    if(empty) ++emptyCompletions;
    return empty;
}

void injectSeaCompletion(std::vector<ProceduralTerrain::Completion>& out)
{
    if(!injectStale) return;
    injectStale=false;staleInjected=true;
    ProceduralTerrain::Completion stale;
    stale.token=0; // Production request tokens start at one: never submitted.
    stale.status=ProceduralTerrain::CompletionStatus::Empty;
    stale.surfaceBiomeMask=1u<<ProceduralTerrain::MarineBiome;
    out.push_back(stale);
}

void checkSea(const ProceduralGeneration& generator,
              const std::vector<ProceduralTerrain::SeaTile>& previous,
              std::uint64_t previousRevision,
              const ProceduralTerrain::SeaTile* previousData)
{
    ++checkedFrames;
    const auto revision=generator.getCoverageRevision();
    const auto& tiles=generator.getSeaTiles();
    require(revision>=previousRevision,"sea coverage revision moved backwards");
    if(revision==previousRevision) {
        require(tiles.size()==previous.size(),"sea count changed before coverage publication");
        require(tiles.empty() || std::memcmp(tiles.data(),previous.data(),
            tiles.size()*sizeof(ProceduralTerrain::SeaTile))==0,
            "sea tiles changed before the complete LOD transaction was ready");
        require(tiles.empty() || tiles.data()==previousData,
            "unchanged coverage reallocates sea tile storage");
    } else ++checkedPublications;

    // Independent coverage oracle: select the horizontal cut from published
    // octree leaves directly, without calling the production sea helper.
    const auto& config=generator.getConfig();
    std::set<TileId> expected;
    for(const auto& chunk:published(generator)) {
        if(config.field.mode!=ProceduralTerrain::FieldMode::Terrain) continue;
        const auto span=std::int64_t(64)<<chunk.lod;
        const double lower=double(chunk.origin[1])*config.cellSize;
        const double upper=double(std::int64_t(chunk.origin[1])+span)*config.cellSize;
        if(!(lower<=config.field.seaLevel && config.field.seaLevel<upper)) continue;
        const bool marine=config.field.biomeOverride>=0
            ? config.field.biomeOverride==int(ProceduralTerrain::MarineBiome)
            : chunk.origin[0]<0 || chunk.origin[2]<0;
        if(!marine) {sawLand=true;continue;}
        require(expected.emplace(chunk.origin[0]/64,chunk.origin[2]/64,chunk.lod).second,
                "published sea cut has duplicate ownership");
    }
    require(tileIds(tiles)==expected,"water does not match published marine coverage, including EMPTY chunks");
    for(std::size_t i=0;i<tiles.size();++i) {
        const auto& a=tiles[i];const auto aw=std::int64_t(1)<<a.lod;
        for(std::size_t j=i+1;j<tiles.size();++j) {
            const auto& b=tiles[j];const auto bw=std::int64_t(1)<<b.lod;
            const auto overlapX=(std::min)(std::int64_t(a.baseX)+aw,std::int64_t(b.baseX)+bw)
                -(std::max)(std::int64_t(a.baseX),std::int64_t(b.baseX));
            const auto overlapZ=(std::min)(std::int64_t(a.baseZ)+aw,std::int64_t(b.baseZ)+bw)
                -(std::max)(std::int64_t(a.baseZ),std::int64_t(b.baseZ));
            require(!(overlapX>0 && overlapZ>0),"coarse and fine water overlap");
            sawMixedLod|=a.lod!=b.lod;
        }
    }
}

void checkInitialSea(const ProceduralGeneration& generator)
{
    require(emptyCompletions>0,"fixture never completed a geometry-empty surface chunk");
    require(!generator.getSeaTiles().empty(),"EMPTY surface chunks lost their water");
    require(generator.getCoverageRevision()>0,"water has no published coverage revision");
    injectStale=true;
}

void checkStoppedSea(const ProceduralGeneration& generator)
{
    require(generator.getSeaTiles().empty(),"stopped terrain retains sea tiles");
    require(generator.getCoverageRevision()==0,"stopped terrain retains coverage revision");
    require(checkedFrames>100 && checkedPublications>1,"fixture did not exercise streaming");
    require(sawLand && sawMixedLod,"fixture lacks dry chunks or mixed sea LODs");
    require(staleInjected,"stale sea completion was not injected");
}
}

namespace Test {
void checkExternalSeaLevels()
{
    Ge::GraphiquePipelineManager pipelines;Ge::CameraManager cameras;Ge::LightManager lights;
    auto* engine=Ge::Engine::getPtrClassAddr();
    engine->graphiquePipelineManager=&pipelines;engine->cameraManager=&cameras;engine->lightManager=&lights;
    auto& camera=cameras.camera;SceneData scene;
    struct Case {ProceduralTerrain::FieldMode mode;float ground,sea;};
    const Case cases[]={{FieldMode::Terrain,-1000.0f,500.0f},
                        {FieldMode::Terrain,1000.0f,-500.0f},
                        {FieldMode::Plane,0.0f,5000.0f},
                        {FieldMode::Sphere,0.0f,-5000.0f}};
    for(const auto& test:cases) {
        ProceduralGeneration::Config config;
        config.field.mode=test.mode;config.field.baseHeight=test.ground;
        config.field.heightAmplitude=0.0f;config.field.detailScale=0.0f;
        config.field.seaLevel=test.sea;config.field.biomeOverride=int(MarineBiome);
        config.maxLod=2;config.viewDistance=256.0f;config.maxChunksPerFrame=4;
        config.maxInFlight=8;config.maxSelectedChunks=1024;config.maxResidentChunks=2048;
        config.collisionEnabled=false;
        const auto terrainBounds=fieldHeightBounds(config.field);
        require(test.sea<terrainBounds.minimumY || test.sea>terrainBounds.maximumY,
                "sea-altitude case does not lie outside terrain bounds");
        const bool terrain=test.mode==FieldMode::Terrain;
        camera.setPosition({-17.0f,(terrain?test.sea:test.ground)+18.0f,31.0f});
        ProceduralGeneration generator(scene,1337);
        require(generator.initialize(config),generator.getLastError());
        const auto beforeEmpty=emptyCompletions;
        settle(generator,camera);
        if(terrain) {
            require(!generator.getSeaTiles().empty(),"sea outside terrain height bounds was not planned");
            require(emptyCompletions>beforeEmpty,"raised/lowered sea had no EMPTY surface completions");
        } else require(generator.getSeaTiles().empty(),"analytic Plane/Sphere produced a sea surface");
        generator.shutdown();
        require(generator.getSeaTiles().empty(),"sea-altitude teardown retained tiles");
    }
}
}

int main()
{
    const int status=terrainSchedulerFixtureMain(0,nullptr);
    if(status) return status;
    try {Test::checkExternalSeaLevels();return 0;}
    catch(const std::exception& error) {
        std::cerr<<"FAIL sea altitude streaming: "<<error.what()<<'\n';return 1;
    }
}
