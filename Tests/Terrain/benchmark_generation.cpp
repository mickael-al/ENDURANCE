// Same benchmark source can be linked against either the original or optimized
// backend. No renderer/physics work or shader compilation is timed as generation.
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifndef TERRAIN_BENCHMARK_SHARED
#include <GLFW/glfw3.h>
#endif

namespace
{
using namespace ProceduralTerrain;
using Clock=std::chrono::steady_clock;
void require(bool value,const std::string& message)
{ if (!value) throw std::runtime_error(message); }
std::string quoted(const std::string& value)
{
    std::string result="\"";
    for (const char c:value)
    {
        if (static_cast<unsigned char>(c)<0x20u)
        {
            constexpr char hex[]="0123456789abcdef";
            result+="\\u00";
            result+=hex[(static_cast<unsigned char>(c)>>4u)&15u];
            result+=hex[static_cast<unsigned char>(c)&15u];
        }
        else
        {
            if (c=='\\' || c=='\"') result+='\\';
            result+=c;
        }
    }
    return result+'\"';
}
void drain(GpuMesher& mesher)
{
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    for (;;)
    {
        std::vector<Completion> ignored;
        mesher.poll(ignored);
        require(ignored.empty(),"Unexpected completion while draining benchmark");
        const auto stats=mesher.stats();
        bool done=stats.allocatedBytes==0;
#ifdef TERRAIN_BENCHMARK_CACHE_API
        done=done && stats.pendingTimingSamples==0;
#endif
        if (done) return;
        require(Clock::now()<deadline,"Benchmark retirement/timing timeout: "+mesher.lastError());
        std::this_thread::yield();
    }
}
std::vector<ChunkRequest> requests(unsigned cells,unsigned seed,unsigned phase)
{
    std::vector<ChunkRequest> result;
    for (int z=0;z<2;++z) for (int x=0;x<4;++x) for (int y=-1;y<=1;++y)
    {
        ChunkRequest request;
        request.origin={(x-2+int(phase==3)*4)*int(cells),y*int(cells),(z-1)*int(cells)};
        request.seed=seed;
        if (phase==1) request.transitionMask=1u<<((x+z)%6);
        result.push_back(request);
    }
    return result;
}
std::string runPhase(GpuMesher& mesher,const std::vector<ChunkRequest>& work,
                     unsigned iteration,const char* name,std::uint64_t& token)
{
    drain(mesher);
    const auto before=mesher.stats();
    std::size_t sent=0,received=0;
    std::uint64_t vertices=0,ready=0,empty=0;
    const auto start=Clock::now();
    const auto deadline=start+std::chrono::minutes(10);
    while (received<work.size())
    {
        while (sent<work.size() && mesher.stats().pendingJobs<mesher.config().maxInFlight)
        {
            require(mesher.submit(work[sent],++token),"Benchmark submit failed: "+mesher.lastError());
            ++sent;
        }
        mesher.dispatchQueued();
        std::vector<Completion> completed;
        mesher.poll(completed);
        for (const auto& result:completed)
        {
            require(result.status==CompletionStatus::Ready || result.status==CompletionStatus::Empty,
                "Benchmark generation failed: "+result.error);
            ++received;
            if (result.status==CompletionStatus::Ready)
            { ++ready; vertices+=result.mesh.vertexCount; mesher.release(result.mesh); }
            else ++empty;
        }
        require(Clock::now()<deadline,"Benchmark generation timeout");
        std::this_thread::yield();
    }
    const double wall=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    drain(mesher);
    require(glGetError()==GL_NO_ERROR,"OpenGL error during benchmark");
    const auto after=mesher.stats();
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(6)<<"{\"iteration\":"<<iteration<<",\"name\":"<<quoted(name)
       <<",\"requests\":"<<work.size()<<",\"wall_ms\":"<<wall<<",\"ready\":"<<ready
       <<",\"empty\":"<<empty<<",\"vertices\":"<<vertices
       <<",\"batches\":"<<after.fieldBatches-before.fieldBatches;
#ifdef TERRAIN_BENCHMARK_CACHE_API
    out<<",\"column_cache_hits\":"<<after.columnCacheHits-before.columnCacheHits
       <<",\"density_cache_hits\":"<<after.densityCacheHits-before.densityCacheHits
       <<",\"column_tiles_built\":"<<after.generatedColumnTiles-before.generatedColumnTiles
       <<",\"density_fields_built\":"<<after.generatedDensityFields-before.generatedDensityFields
       <<",\"biome_dispatches\":"<<after.submittedBiomePasses-before.submittedBiomePasses
       <<",\"gpu_2d_ms\":"<<after.field2DMilliseconds-before.field2DMilliseconds
       <<",\"gpu_prepare_ms\":"<<after.fieldPreparationMilliseconds-before.fieldPreparationMilliseconds
       <<",\"gpu_volume_ms\":"<<after.volumeMilliseconds-before.volumeMilliseconds
       <<",\"gpu_count_ms\":"<<after.countMilliseconds-before.countMilliseconds
       <<",\"gpu_emit_ms\":"<<after.emissionMilliseconds-before.emissionMilliseconds
       <<",\"dropped_timing_samples\":"<<after.droppedTimingSamples-before.droppedTimingSamples
       <<",\"gpu_timings_complete\":"<<(after.droppedTimingSamples==before.droppedTimingSamples?"true":"false");
#endif
    return out.str()+"}";
}
std::string benchmark(const char* shaders,unsigned cells,unsigned jobs,unsigned iterations,int biome,bool caches)
{
    glewExperimental=GL_TRUE;
    const auto status=glewInit();
    require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"Cannot initialize GLEW");
    require(GLEW_VERSION_4_5,"Benchmark requires OpenGL 4.5");
    while (glGetError()!=GL_NO_ERROR) {}
    require(iterations>0 && iterations<=100,"Iterations must be in [1,100]");
    GpuConfig config;
    config.cells=cells; config.maxInFlight=jobs; config.maxResidentChunks=256;
    config.vertexBudgetMiB=256; config.shaderDirectory=shaders;
    // Bound relief relative to N so all supported resolutions include actual
    // surfaces as well as solid/empty neighbours in the three vertical chunks.
    // The same parameters are used by both backends, never inferred from output.
    config.field.baseHeight=0.5f*cells;
    config.field.seaLevel=config.field.baseHeight;
    config.field.heightAmplitude=0.125f*cells;
    config.field.detailScale=0.05f*static_cast<float>(cells)/64.0f;
    config.field.biomeOverride=biome;
#ifdef TERRAIN_BENCHMARK_CACHE_API
    if (!caches) config.columnCacheEntries=config.densityCacheEntries=0;
#else
    require(caches,"This backend has no cache-disabling API");
#endif
    std::string error;
    GpuMesher mesher;
    const auto initializeStart=Clock::now();
    require(mesher.initialize(config,error),error);
    const double initializeMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-initializeStart).count();
    const auto glString=[](GLenum name)
    { const auto* value=glGetString(name); return value?reinterpret_cast<const char*>(value):"unknown"; };
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(6)<<"{\"renderer\":"<<quoted(glString(GL_RENDERER))
       <<",\"vendor\":"<<quoted(glString(GL_VENDOR))<<",\"opengl\":"<<quoted(glString(GL_VERSION))
       <<",\"cells\":"<<cells<<",\"jobs\":"<<jobs<<",\"biome\":"<<biome
       <<",\"base_height\":"<<config.field.baseHeight
       <<",\"height_amplitude\":"<<config.field.heightAmplitude
       <<",\"detail_scale\":"<<config.field.detailScale
       <<",\"iterations\":"<<iterations<<",\"initialize_ms\":"<<initializeMilliseconds;
#ifdef TERRAIN_BENCHMARK_CACHE_API
    out<<",\"optimized_backend\":true,\"cache_retention\":"<<(caches?"true":"false");
#else
    out<<",\"optimized_backend\":false,\"cache_retention\":false";
#endif
    out<<",\"phases\":[";
    std::uint64_t token=0;
    bool first=true;
    const char* names[]={"cold_fields","transition_rebuild","revisit","new_columns"};
    for (unsigned iteration=0;iteration<iterations;++iteration)
    {
        // Warm the exact seeds, programs and transition masks used below.
        // Reset only field retention, so lazy driver JIT does not get charged
        // to one backend's cold-field generation. The baseline has no cache.
        for (unsigned phase=0;phase<4;++phase)
            runPhase(mesher,requests(cells,1337+iteration,phase),iteration,"warmup",token);
#ifdef TERRAIN_BENCHMARK_CACHE_API
        require(mesher.clearFieldCaches(),"Cannot clear idle benchmark fields");
#endif
        for (unsigned phase=0;phase<4;++phase)
        {
            if (!first) out<<',';
            first=false;
            out<<runPhase(mesher,requests(cells,1337+iteration,phase),iteration,names[phase],token);
        }
    }
    const auto stats=mesher.stats();
    out<<"],\"field_scratch_bytes\":"<<stats.transientFieldBytes;
#ifdef TERRAIN_BENCHMARK_CACHE_API
    out<<",\"field_cache_bytes\":"<<stats.cachedFieldBytes<<",\"group_metadata_bytes\":"<<stats.groupMetadataBytes;
#endif
    out<<"}";
    return out.str();
}
}

#ifdef TERRAIN_BENCHMARK_SHARED
extern "C" const char* terrain_generation_benchmark(const char* shaders,unsigned cells,unsigned jobs,
                                                    unsigned iterations,int biome,int caches)
{
    static std::string result;
    try { result=benchmark(shaders,cells,jobs,iterations,biome,caches!=0); }
    catch (const std::exception& error) { result="{\"error\":"+quoted(error.what())+"}"; }
    return result.c_str();
}
#else
int main(int argc,char** argv)
{
    GLFWwindow* window=nullptr;
    try
    {
        std::string shaders=TERRAIN_DEFAULT_SHADERS,output;
        unsigned cells=64,jobs=3,iterations=3;
        int biome=-1;
        bool caches=true;
        for (int i=1;i<argc;++i)
        {
            const std::string option=argv[i];
            if (option=="--disable-caches") { caches=false; continue; }
            require(i+1<argc,"Missing value for "+option);
            const std::string value=argv[++i];
            if (option=="--shaders") shaders=value;
            else if (option=="--output") output=value;
            else if (option=="--cells") cells=static_cast<unsigned>(std::stoul(value));
            else if (option=="--jobs") jobs=static_cast<unsigned>(std::stoul(value));
            else if (option=="--iterations") iterations=static_cast<unsigned>(std::stoul(value));
            else if (option=="--biome") biome=std::stoi(value);
            else throw std::runtime_error("Unknown option "+option);
        }
        require(glfwInit()==GLFW_TRUE,"Cannot initialize GLFW");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        window=glfwCreateWindow(32,32,"Terrain generation benchmark",nullptr,nullptr);
        require(window!=nullptr,"Cannot create OpenGL 4.5 benchmark context");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        const auto result=benchmark(shaders.c_str(),cells,jobs,iterations,biome,caches);
        if (output.empty()) std::cout<<result<<'\n';
        else { std::ofstream file(output); file<<result<<'\n'; require(file.good(),"Cannot save "+output); }
        glfwDestroyWindow(window); glfwTerminate();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr<<"Terrain benchmark: "<<error.what()<<'\n';
        if (window) glfwDestroyWindow(window);
        glfwTerminate(); return 1;
    }
}
#endif
