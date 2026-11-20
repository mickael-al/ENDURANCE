// Real backend completions: marine metadata must outlive empty meshing and
// field-cache reuse. No new CPU climate implementation is used as an oracle.
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ProceduralTerrain;
namespace
{
constexpr auto marineBit=std::uint32_t{1}<<MarineBiome;
unsigned blockingWaits=0,finishes=0;
PFNGLCLIENTWAITSYNCPROC originalWait=nullptr;
GLenum GLAPIENTRY observeWait(GLsync fence,GLbitfield flags,GLuint64 timeout)
{
    if(timeout!=0) ++blockingWaits;
    return originalWait(fence,flags,timeout);
}
void require(bool ok,const std::string& error)
{ if(!ok) throw std::runtime_error(error); }
Completion finish(GpuMesher& mesher)
{
    std::vector<Completion> completed;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    do
    {
        mesher.poll(completed);
        std::this_thread::yield();
    } while(completed.empty() && std::chrono::steady_clock::now()<deadline);
    require(completed.size()==1,"Missing or duplicate completion: "+mesher.lastError());
    require(glGetError()==GL_NO_ERROR,"GL error during sea metadata completion");
    return completed.front();
}
Completion one(GpuMesher& mesher,const ChunkRequest& request,std::uint64_t token)
{
    require(mesher.submit(request,token),mesher.lastError());
    return finish(mesher);
}
void checkCase(const std::string& directory,float height,int forced,
    bool marine,bool land,FieldMode mode=FieldMode::Terrain,bool expectMesh=false)
{
    GpuConfig config;
    config.shaderDirectory=directory;
    config.cells=4;config.maxInFlight=2;config.maxResidentChunks=4;
    config.columnCacheEntries=4;config.densityCacheEntries=4;config.vertexBudgetMiB=1;
    config.measureGenerationTimings=false;
    config.field.mode=mode;config.field.baseHeight=height;
    config.field.heightAmplitude=0;config.field.detailScale=0;
    config.field.biomeOverride=forced;config.field.sphereCenter={{100,100,100}};
    config.field.sphereRadius=1;
    GpuMesher mesher;
    std::string error;
    require(mesher.initialize(config,error),error);
    ChunkRequest request;
    const auto initial=one(mesher,request,1);
    require(initial.status==(expectMesh?CompletionStatus::Ready:CompletionStatus::Empty),
        "Unexpected initial geometry state");
    const auto mask=initial.surfaceBiomeMask;
    require(bool(mask&marineBit)==marine,"Wrong marine bit on completion");
    require(bool(mask&~marineBit)==land,"Wrong land bits on completion");
    if(mode==FieldMode::Terrain && forced>=0)
        require(mask==(std::uint32_t{1}<<forced),"Forced-biome mask contains other biomes");
    if(!expectMesh) require(initial.mesh.vertexCount==0,"Empty chunk acquired geometry");
    const auto initialStats=mesher.stats();

    const auto repeated=one(mesher,request,2);
    require(repeated.status==initial.status && repeated.surfaceBiomeMask==mask,
        "Density-cache reuse lost metadata");
    const auto repeatedStats=mesher.stats();
    require(repeatedStats.generatedColumnTiles==initialStats.generatedColumnTiles &&
        repeatedStats.generatedDensityFields==initialStats.generatedDensityFields &&
        repeatedStats.columnCacheHits>initialStats.columnCacheHits &&
        repeatedStats.densityCacheHits>initialStats.densityCacheHits,
        "Repeat did not exercise both field-cache hits");

    // Same XZ tile, distinct vertical chunk: reuse columns, rebuild density.
    request.origin[1]=16;
    const auto upper=one(mesher,request,3);
    require(upper.surfaceBiomeMask==mask,"Column-cache reuse lost metadata");
    const auto upperStats=mesher.stats();
    require(upperStats.generatedColumnTiles==repeatedStats.generatedColumnTiles &&
        upperStats.generatedDensityFields==repeatedStats.generatedDensityFields+1 &&
        upperStats.columnCacheHits>repeatedStats.columnCacheHits,
        "Vertical neighbor did not exercise column-only cache reuse");
    if(initial.status==CompletionStatus::Ready) mesher.release(initial.mesh);
    if(repeated.status==CompletionStatus::Ready) mesher.release(repeated.mesh);
    if(upper.status==CompletionStatus::Ready) mesher.release(upper.mesh);
}
}

extern "C" void GLAPIENTRY __real_glFinish();
extern "C" void GLAPIENTRY __wrap_glFinish()
{ ++finishes;__real_glFinish(); }

extern "C" int sea_metadata_tests(const char* shaderDirectory)
{
    try
    {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW init failed");
        while(glGetError()!=GL_NO_ERROR) {}
        originalWait=__glewClientWaitSync;__glewClientWaitSync=observeWait;
        checkCase(shaderDirectory,-64,25,true,false);  // forced sea, no geometry
        checkCase(shaderDirectory,2,25,true,false,FieldMode::Terrain,true); // emission path
        checkCase(shaderDirectory,-64,-1,true,false); // natural sea
        checkCase(shaderDirectory,0,-1,true,true);    // mixed biome footprint
        checkCase(shaderDirectory,128,-1,false,true);// natural land
        checkCase(shaderDirectory,-64,2,false,true); // forced land below sea level
        checkCase(shaderDirectory,-64,25,false,false,FieldMode::Plane);
        checkCase(shaderDirectory,-64,25,false,false,FieldMode::Sphere);
        __glewClientWaitSync=originalWait;
        require(finishes==0 && blockingWaits==0,"Sea metadata introduced a blocking GPU wait");
        std::cout<<"PASS sea metadata: Ready/Empty, forced/natural/mixed biomes, column/density caches, no blocking waits\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        if(originalWait) __glewClientWaitSync=originalWait;
        std::cerr<<error.what()<<'\n';return 1;
    }
}
