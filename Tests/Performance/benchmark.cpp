// Bounded, reproducible backend/renderer measurements. This does not pretend
// that an isolated tree pass is the FPS of the complete game.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "TreeRenderGpu.hpp"
#include "EnduranceObjectPlacement.hpp"
#include "ShaderCompilation.hpp"
#include "hv/cpputil/json.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>
#ifndef ENDURANCE_PERFORMANCE_SHARED
#include <GLFW/glfw3.h>
#endif

namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
using namespace ProceduralTerrain;
using namespace EnduranceTrees;
double ms(Clock::time_point a,Clock::time_point b=Clock::now())
{ return std::chrono::duration<double,std::milli>(b-a).count(); }
void check(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
std::string glText(GLenum name) { auto p=glGetString(name); return p?reinterpret_cast<const char*>(p):"unknown"; }
struct Target
{
    GLuint fbo=0,color[4]{},depth=0; unsigned size=320;
    explicit Target(unsigned resolution):size(resolution)
    {
        glCreateFramebuffers(1,&fbo); glCreateTextures(GL_TEXTURE_2D,4,color); glCreateTextures(GL_TEXTURE_2D,1,&depth);
        const GLenum formats[]={GL_RG32F,GL_RGB16F,GL_RGBA8,GL_RGB8};
        const GLenum attachments[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
        for(unsigned i=0;i<4;++i) { glTextureStorage2D(color[i],1,formats[i],size,size); glNamedFramebufferTexture(fbo,attachments[i],color[i],0); }
        glTextureStorage2D(depth,1,GL_DEPTH_COMPONENT32F,size,size); glNamedFramebufferTexture(fbo,GL_DEPTH_ATTACHMENT,depth,0);
        glNamedFramebufferDrawBuffers(fbo,4,attachments);
        check(glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete benchmark target");
    }
    ~Target() { glDeleteFramebuffers(1,&fbo); glDeleteTextures(4,color); glDeleteTextures(1,&depth); }
    void begin()
    {
        glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,size,size);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        float zero[4]{},one=1;
        for(unsigned i=0;i<4;++i) glClearNamedFramebufferfv(fbo,GL_COLOR,i,zero);
        glClearNamedFramebufferfv(fbo,GL_DEPTH,0,&one);
    }
};
struct Scene
{
    GpuConfig gpu; LodConfig lod; glm::vec3 camera{}; unsigned seed=1337;
};
Scene sceneSettings(const Json& options)
{
    Scene s; const auto& data=options.at("terrain");
    s.gpu.cells=options.value("cells",64u); s.gpu.baseSpacing=data.value("cellSize",1.0f)*64.0f/s.gpu.cells;
    s.gpu.maxInFlight=options.value("jobs",data.value("maxInFlight",3u));
    s.gpu.vertexBudgetMiB=options.value("vertex_budget_mib",data.value("vertexBudgetMiB",1024u)); s.gpu.maxResidentChunks=data.value("maxResidentChunks",4096u);
    s.gpu.columnCacheEntries=data.value("columnCacheEntries",128u); s.gpu.densityCacheEntries=data.value("densityCacheEntries",32u);
    if(options.value("disable_caches",false)) s.gpu.columnCacheEntries=s.gpu.densityCacheEntries=0;
    s.gpu.shaderDirectory=options.at("root").get<std::string>()+"/Asset/Shader/ProceduralTerrain";
    auto& p=s.gpu.field;
    p.baseHeight=data.value("baseHeight",p.baseHeight); p.heightAmplitude=data.value("heightAmplitude",p.heightAmplitude);
    p.heightFrequency=data.value("heightFrequency",p.heightFrequency); p.bandOffset=data.value("bandOffset",p.bandOffset);
    p.bandOffsetVariation=data.value("bandOffsetVariation",p.bandOffsetVariation); p.detailScale=data.value("detailScale",p.detailScale);
    p.seaLevel=data.value("seaLevel",p.seaLevel); p.seaBlendWidth=data.value("seaBlendWidth",p.seaBlendWidth);
    p.biomeOverride=options.value("biome",data.value("biomeOverride",-1));
    if(options.value("plane",false)) { p.mode=FieldMode::Plane; p.baseHeight=128.5f; }
    s.seed=data.value("seed",1337u);
    s.lod.cells=64; s.lod.baseSpacing=data.value("cellSize",1.0f); s.lod.maxLod=data.value("maxLod",6u);
    s.lod.viewDistance=data.value("viewDistance",40096.0f); s.lod.splitDistanceFactor=data.value("lodDistanceFactor",1.25f);
    s.lod.fullResolutionRadius=data.value("collisionEnabled",false)?data.value("collisionRadius",128.0f):0;
    s.lod.maxLeaves=data.value("maxSelectedChunks",3072u);
    auto bounds=fieldHeightBounds(p); const float padding=std::ldexp(s.lod.baseSpacing,static_cast<int>(s.lod.maxLod));
    s.lod.minimumY=(std::min)(bounds.minimumY,p.seaLevel)-padding; s.lod.maximumY=(std::max)(bounds.maximumY,p.seaLevel)+padding;
    auto c=options.at("camera"); s.camera={c.at(0).get<float>(),c.at(1).get<float>(),c.at(2).get<float>()};
    return s;
}
std::vector<LodChunk> selection(const Scene& scene,Json& report)
{
    std::vector<LodChunk> result; std::string error; const auto start=Clock::now();
    check(selectLodChunks(scene.camera,scene.lod,result,error),error);
    report["selection_cpu_ms"]=ms(start); report["selected_chunks"]=result.size();
    std::array<unsigned,13> counts{}; std::uint64_t hash=1469598103934665603ull;
    for(const auto& chunk:result)
    {
        ++counts[chunk.lod];
        for(auto x:chunk.origin) hash=(hash^static_cast<std::uint32_t>(x))*1099511628211ull;
        hash=(hash^chunk.lod)*1099511628211ull; hash=(hash^chunk.transitionMask)*1099511628211ull;
    }
    report["lod_histogram"]=counts; report["selection_hash"]=hash;
    report["full_resolution_voxel_cells"]=std::uint64_t{result.size()}*64*64*64;
    report["view_distance_m"]=scene.lod.viewDistance;
    report["height_bounds_m"]={scene.lod.minimumY,scene.lod.maximumY};
    std::stable_sort(result.begin(),result.end(),[&](const LodChunk& a,const LodChunk& b){return chunkDistanceSquared(scene.camera,a,64,scene.lod.baseSpacing)<chunkDistanceSquared(scene.camera,b,64,scene.lod.baseSpacing);});
    return result;
}
RenderView cameraView(glm::vec3 camera,glm::vec3 target,float far=90000)
{
    RenderView view; view.camera=camera; view.view=glm::lookAt(camera,target,glm::vec3(0,1,0));
    auto projection=glm::perspective(glm::radians(60.0f),1.0f,.1f,far);
    view.clip=projection*view.view; view.projectionScale=projection[1][1]; return view;
}
Json renderStats(const RenderStats& s)
{
    Json j={{"loaded_models",s.loadedModels},{"pending_models",s.pendingModels},{"failed_models",s.failedModels},
        {"instances",s.instances},{"transform_builds",s.transformBuilds},{"cull_dispatches",s.cullDispatches},
        {"draw_calls",s.drawCalls},{"model_bytes",s.modelBytes},{"working_bytes",s.workingBytes},{"uploads",s.assetUploads}};
#ifdef ENDURANCE_PERF_INCREMENTAL_TREES
    j["transformed_instances"]=s.transformedInstances;
    j["transform_pages_built"]=s.transformPagesBuilt; j["transform_pages_reused"]=s.transformPagesReused;
    j["indirect_commands"]=s.indirectCommands;
#endif
    return j;
}
Json trees(const Json& options)
{
    const auto root=std::filesystem::path(options.at("root").get<std::string>());
    ObjectPlacementConfig catalogue; std::string error;
    check(Endurance::loadObjectPlacement((root/"ENDURANCE/Ressources/Trees/object_placement.json").string(),catalogue,error),error);
    const unsigned modelCount=options.value("models",1u),count=options.value("instances",64u),frames=options.value("frames",8u);
    check(modelCount && modelCount<=catalogue.prototypes.size() && count>(modelCount-1)*ObjectPageSize && count<=65536 && frames>0 && frames<=1000,"Invalid tree benchmark size");
    std::vector<ObjectPrototype> prototypes(catalogue.prototypes.begin(),catalogue.prototypes.begin()+modelCount);
    for(auto& p:prototypes) { p.minimumScale=p.maximumScale=1; p.capacity=65536; }
    RenderSettings settings; settings.shaderDirectory=(root/"ENDURANCE/Ressources/Shader/Trees").string(); settings.modelBudgetMiB=512;
    TreeRenderGpu renderer; const auto init=Clock::now(); check(renderer.initialize(prototypes,settings),renderer.error());
    Json report={{"initialize_ms",ms(init)},{"fixture","fixed baked models, scale1, hashed positions, actual G-buffer pass"}};
    std::vector<PackedObjectPosition> points; std::vector<ObjectBlock> blocks;
    for(unsigned first=0;first<count;first+=ObjectPageSize)
    {
        ObjectBlock b; b.prototype=(first/ObjectPageSize)%modelCount; b.firstInstance=first; b.instanceCount=(std::min)(ObjectPageSize,count-first);
        b.origin={{-512,-64,-512}}; b.extent=1024; b.seed=1337;
#ifdef ENDURANCE_PERF_ALLOCATION_REVISION
        b.allocationRevision=first+1;
#endif
        for(unsigned i=0;i<b.instanceCount;++i)
        {
            const unsigned id=first+i; const auto hx=objectHash(id^1u),hz=objectHash(id^9u);
            // Fixed world sample reused identically in baseline/candidate.
            points.push_back({(hx&65535u)|(4096u<<16u),(hz&65535u)|(id<<16u)});
        }
        blocks.push_back(b);
    }
    GLuint buffer=0; glCreateBuffers(1,&buffer); glNamedBufferData(buffer,points.size()*8,points.data(),GL_STATIC_DRAW);
    auto view=cameraView({0,160,900},{0,30,0}); Target target(options.value("resolution",320u));
    const auto load=Clock::now(); auto until=load+std::chrono::seconds(120);
    while(renderer.stats().loadedModels<modelCount || renderer.stats().pendingModels)
    {
        renderer.prepare(buffer,blocks,1,view); check(renderer.error().empty(),renderer.error());
        check(Clock::now()<until,"Tree loading timed out"); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    glFinish(); report["asset_load_upload_wall_ms"]=ms(load);
    report["phases"]=Json::array();
    for(const std::string phase:{"static","camera_motion","publication_churn"})
    {
        for(unsigned warm=0;warm<2;++warm) { target.begin(); renderer.prepare(buffer,blocks,1,view); renderer.draw(0,view.clip); }
        glFinish(); const auto before=renderer.stats(); std::vector<GLuint> queries(frames),primitiveQueries(frames);
        glGenQueries(frames,queries.data()); glGenQueries(frames,primitiveQueries.data());
        std::vector<double> cpu; const auto start=Clock::now();
        for(unsigned frame=0;frame<frames;++frame)
        {
            auto v=view; if(phase=="camera_motion") v=cameraView({float(frame)*.5f,160,900},{0,30,0});
            const auto time=Clock::now(); glBeginQuery(GL_TIME_ELAPSED,queries[frame]); glBeginQuery(GL_PRIMITIVES_GENERATED,primitiveQueries[frame]); target.begin();
            renderer.prepare(buffer,blocks,phase=="publication_churn"?frame+2u:1u,v); renderer.draw(0,v.clip);
            if(options.value("shadows",false))
            {
                const auto light=glm::ortho(-600.f,600.f,-600.f,600.f,.1f,3000.f)*glm::lookAt(glm::vec3(600,1200,600),glm::vec3(0),glm::vec3(0,1,0));
                target.begin();
                renderer.draw(1,light);
            }
            glEndQuery(GL_PRIMITIVES_GENERATED); glEndQuery(GL_TIME_ELAPSED); cpu.push_back(ms(time));
        }
        glFinish(); const double elapsed=ms(start); std::vector<double> gpu;
        for(GLuint q:queries) { GLuint64 n=0; glGetQueryObjectui64v(q,GL_QUERY_RESULT,&n); gpu.push_back(double(n)/1e6); }
        std::vector<std::uint64_t> primitives;
        for(GLuint q:primitiveQueries) { GLuint64 n=0; glGetQueryObjectui64v(q,GL_QUERY_RESULT,&n); primitives.push_back(n); }
        glDeleteQueries(frames,queries.data()); glDeleteQueries(frames,primitiveQueries.data()); const auto after=renderer.stats();
        report["phases"].push_back({{"name",phase},{"frames",frames},{"wall_ms",elapsed},{"cpu_submit_ms",cpu},{"gpu_elapsed_ms",gpu},
            {"transform_builds",after.transformBuilds-before.transformBuilds},{"cull_dispatches",after.cullDispatches-before.cullDispatches},
            {"draw_calls",after.drawCalls-before.drawCalls},{"triangles",primitives},{"stats_before",renderStats(before)},{"stats_after",renderStats(after)}});
    }
    report["instances"]=count; report["models"]=modelCount; report["resolution"]=target.size;
    renderer.shutdown(); glDeleteBuffers(1,&buffer);
    return report;
}
Json generation(const Json& options)
{
    const auto settings=sceneSettings(options); Json report; auto selected=selection(settings,report); auto config=settings.gpu;
    const unsigned maximum=options.value("max_requests",32u);
    if(maximum && selected.size()>maximum) selected.resize(maximum);
    const unsigned factor=64/config.cells;
    std::vector<ChunkRequest> requests;
    for(const auto& c:selected)
    { ChunkRequest r; r.origin=c.origin; for(auto& x:r.origin)x/=static_cast<int>(factor); r.lod=c.lod; r.transitionMask=c.transitionMask; r.seed=settings.seed; requests.push_back(r); }
    const bool objects=options.value("objects",false);
    if(objects)
    {
        std::string error; check(Endurance::loadObjectPlacement(options.at("root").get<std::string>()+"/ENDURANCE/Ressources/Trees/object_placement.json",config.objects,error),error);
    }
    report["requested_chunks"]=requests.size(); report["cells"]=config.cells; report["base_spacing_m"]=config.baseSpacing;
    report["jobs"]=config.maxInFlight; report["objects_enabled"]=objects;
    report["same_sampling_as_scene"]=config.cells==64;
    GpuMesher mesher; std::string error; const auto init=Clock::now(); check(mesher.initialize(config,error),error); glFinish(); report["initialize_ms"]=ms(init);
    std::size_t sent=0,received=0; unsigned ready=0,empty=0,peakPending=0,polls=0,frames=0;
    std::vector<MeshHandle> meshes; std::vector<LodChunk> published; std::vector<double> cpuSubmits,cpuPolls,cpuDispatches;
    Target target(options.value("resolution",320u));
    GLuint simple=0,cameraBuffer=0;
    if(options.value("render_during_generation",false))
    {
        const auto root=options.at("root").get<std::string>(); const auto harness=options.at("harness").get<std::string>();
        simple=Ge::buildShaderProgram({{GL_VERTEX_SHADER,root+"/Asset/Shader/ProceduralTerrain/render.vs.glsl",{}},
            {GL_FRAGMENT_SHADER,harness+"/terrain_probe.fs.glsl",{}}},error,"Terrain mixed benchmark",{true,{}}); check(simple!=0,error);
        auto view=cameraView(settings.camera,settings.camera+glm::vec3(0,-.3f,-1));
        struct Uniforms { glm::vec4 camera; glm::mat4 view,inverse,projection; } u;
        u.camera=glm::vec4(view.camera,0); u.view=view.view; u.inverse=glm::inverse(view.view); u.projection=view.clip*u.inverse;
        glCreateBuffers(1,&cameraBuffer); glNamedBufferData(cameraBuffer,sizeof(u),&u,GL_STATIC_DRAW);
    }
    glFinish();
    const auto start=Clock::now(); const auto deadline=start+std::chrono::seconds(options.value("timeout_seconds",180u));
    std::vector<GLsync> renderFences; auto nextFrame=Clock::now(); std::string failure;
    while(received<requests.size())
    {
        if(Clock::now()>deadline) { failure="timeout"; break; }
        while(sent<requests.size() && mesher.stats().pendingJobs<config.maxInFlight)
        {
            const auto begin=Clock::now(); const bool ok=mesher.submit(requests[sent],sent+1); cpuSubmits.push_back(ms(begin));
            if(!ok) { failure=mesher.lastError().empty()?"submit rejected without backend detail":mesher.lastError(); break; } ++sent;
        }
        if(!failure.empty()) break;
        const auto dispatchStart=Clock::now(); mesher.dispatchQueued(); cpuDispatches.push_back(ms(dispatchStart));
        peakPending=(std::max)(peakPending,mesher.stats().pendingJobs);
        std::vector<Completion> done; const auto begin=Clock::now(); mesher.poll(done); cpuPolls.push_back(ms(begin)); ++polls;
        for(const auto& c:done)
        {
            if(c.status!=CompletionStatus::Ready && c.status!=CompletionStatus::Empty) { failure=c.error.empty()?"non-successful completion without backend detail":c.error; break; }
            ++received; auto chunk=selected.at(static_cast<std::size_t>(c.token-1));
            // Publication lattice is the sampled backend lattice.
            for(auto& x:chunk.origin)x/=static_cast<int>(factor); published.push_back(chunk);
            if(c.status==CompletionStatus::Ready) { ++ready; meshes.push_back(c.mesh); } else ++empty;
        }
        if(!failure.empty()) break;
        if(!done.empty() && objects) mesher.publishObjectCoverage(published,settings.seed);
        if(simple && Clock::now()>=nextFrame)
        {
            while(!renderFences.empty() && glClientWaitSync(renderFences.front(),0,0)!=GL_TIMEOUT_EXPIRED)
            { glDeleteSync(renderFences.front()); renderFences.erase(renderFences.begin()); }
            if(renderFences.size()<2)
            {
                target.begin(); glUseProgram(simple); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,cameraBuffer);
                mesher.drawMany(meshes); renderFences.push_back(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0)); ++frames;
            }
            nextFrame=Clock::now()+std::chrono::milliseconds(25);
        }
        std::this_thread::yield();
    }
    glFinish(); const double elapsed=ms(start);
    // Timers are asynchronous; finish then poll once to include completed queries.
    std::vector<Completion> tail; mesher.poll(tail); const auto s=mesher.stats(); const auto o=mesher.objectStats();
    const bool completed=failure.empty() && received==requests.size();
    report["completed_fixture"]=completed;
    report["complete_scene_coverage"]=completed && requests.size()==report.at("selected_chunks").get<std::size_t>() && config.cells==64;
    report["failure"]=failure; report["wall_ms"]=elapsed; report["submitted"]=sent; report["completed"]=received;
    report["ready"]=ready; report["empty"]=empty; report["failed_jobs"]=s.failedJobs; report["resident_vertices"]=s.residentVertices;
    report["resident_mesh_bytes"]=s.allocatedBytes; report["mesh_budget_bytes"]=s.budgetBytes;
    report["transient_field_bytes"]=s.transientFieldBytes; report["cached_field_bytes"]=s.cachedFieldBytes;
    report["biome_metadata_bytes"]=s.biomeMetadataBytes; report["group_metadata_bytes"]=s.groupMetadataBytes;
    report["object_pool_bytes"]=o.poolBytes; report["object_scratch_bytes"]=o.scratchBytes; report["object_capture_jobs"]=o.captureJobs;
    report["object_generated_instances"]=o.generatedInstances; report["object_cached_jobs"]=o.cachedJobs; report["peak_pending_jobs"]=peakPending; report["polls"]=polls; report["interleaved_render_frames"]=frames;
    const auto describe=[](std::vector<double> values)
    { if(values.empty()) return Json{{"count",0}}; std::sort(values.begin(),values.end()); return Json{{"count",values.size()},{"sum_ms",std::accumulate(values.begin(),values.end(),0.0)},{"median_ms",values[values.size()/2]},{"p95_ms",values[(values.size()-1)*95/100]},{"max_ms",values.back()}}; };
    report["cpu_submit"]=describe(cpuSubmits); report["cpu_poll"]=describe(cpuPolls); report["cpu_dispatch"]=describe(cpuDispatches);
    report["driver_lazy_work_may_be_included"]=true;
    report["gpu_stage_ms"]={{"columns",s.field2DMilliseconds},{"preparation",s.fieldPreparationMilliseconds},{"volume",s.volumeMilliseconds},{"count",s.countMilliseconds},{"emit",s.emissionMilliseconds}};
    report["gpu_timer_samples_dropped"]=s.droppedTimingSamples; report["gpu_timer_samples_pending"]=s.pendingTimingSamples;
    report["field_batches"]=s.fieldBatches; report["column_tiles_built"]=s.generatedColumnTiles; report["density_fields_built"]=s.generatedDensityFields;
    report["column_cache_hits"]=s.columnCacheHits; report["density_cache_hits"]=s.densityCacheHits;
    report["object_instances"]=o.instances; report["object_blocks"]=o.blocks; report["object_revision"]=o.revision; report["object_error"]=mesher.objectError();
    for(auto fence:renderFences) glDeleteSync(fence);
    if(simple) glDeleteProgram(simple); if(cameraBuffer) glDeleteBuffers(1,&cameraBuffer);
    return report;
}
Json run(const Json& options)
{
    const auto mode=options.at("scenario").get<std::string>();
    Json report={{"scenario",mode},{"options",options},{"scope","isolated production subsystems; not full game FPS or full scene streaming scheduler"}};
    if(mode=="selection") { const auto scene=sceneSettings(options); selection(scene,report); return report; }
    glewExperimental=GL_TRUE; auto status=glewInit(); check(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
    while(glGetError()!=GL_NO_ERROR) {}
    report["renderer"]=glText(GL_RENDERER); report["vendor"]=glText(GL_VENDOR); report["opengl"]=glText(GL_VERSION);
    const auto renderer=report["renderer"].get<std::string>(); report["software_renderer"]=renderer.find("llvmpipe")!=std::string::npos || renderer.find("softpipe")!=std::string::npos;
    if(mode=="trees") report["result"]=trees(options);
    else if(mode=="generation") report["result"]=generation(options);
    else throw std::runtime_error("Unknown scenario: "+mode);
    check(glGetError()==GL_NO_ERROR,"OpenGL error during performance fixture");
    return report;
}
}
#ifdef ENDURANCE_PERFORMANCE_SHARED
extern "C" const char* endurance_performance(const char* options)
{
    static std::string result;
    try { result=run(Json::parse(options)).dump(); }
    catch(const std::exception& e) { result=Json{{"error",e.what()}}.dump(); }
    return result.c_str();
}
#else
int main(int argc,char** argv)
{
    GLFWwindow* window=nullptr;
    try
    {
        check(argc==3,"Usage: endurance_performance config.json report.json"); std::ifstream input(argv[1]); Json options; input>>options;
        if(options.at("scenario")!="selection")
        {
            check(glfwInit()==GLFW_TRUE,"GLFW initialization failed"); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,6); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
            window=glfwCreateWindow(32,32,"ENDURANCE performance",nullptr,nullptr); check(window,"Cannot create desktop GL4.6 context");
            glfwMakeContextCurrent(window); glfwSwapInterval(0);
        }
        auto report=run(options); std::ofstream output(argv[2]); output<<report.dump(2)<<'\n'; check(output.good(),"Cannot write report");
        const bool incomplete=options.at("scenario")=="generation" && !report.at("result").at("completed_fixture").get<bool>();
        if(window) { glfwDestroyWindow(window); glfwTerminate(); } return incomplete?2:0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; if(window){glfwDestroyWindow(window);glfwTerminate();} return 1; }
}
#endif
