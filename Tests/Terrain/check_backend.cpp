// Loaded by check_backend.py after creating an EGL context. It exercises the
// production C++ scheduler/allocation lifecycle, not a duplicate implementation.
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ProceduralTerrain;
namespace
{
void require(bool condition,const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
std::vector<Completion> finish(GpuMesher& mesher,unsigned expected)
{
    std::vector<Completion> completed;
    // Software drivers can JIT a fixed variant at its first dispatch, even
    // after a successful link. This is a correctness timeout, not a benchmark.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while (completed.size()<expected && std::chrono::steady_clock::now()<deadline)
    {
        mesher.poll(completed);
        std::this_thread::yield();
    }
    require(completed.size()==expected,"Async backend did not complete jobs within 60s (completed "+
        std::to_string(completed.size())+"/"+std::to_string(expected)+
        ", pending "+std::to_string(mesher.stats().pendingJobs)+
        ", batches "+std::to_string(mesher.stats().fieldBatches)+
        "): "+mesher.lastError());
    require(glGetError()==GL_NO_ERROR,"OpenGL error during asynchronous backend lifecycle");
    return completed;
}
Completion one(GpuMesher& mesher,const ChunkRequest& request,std::uint64_t token)
{
    require(mesher.submit(request,token),"submit: "+mesher.lastError());
    return finish(mesher,1).front();
}
void collectRetired(GpuMesher& mesher,std::uint64_t expectedAllocated=0)
{
    // No swap, glFinish or generation job drives this retirement: poll must
    // submit its release fences and make progress on its own.
    require(mesher.stats().pendingJobs==0,"Retirement test requires idle jobs");
    std::vector<Completion> ignored;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do
    {
        mesher.poll(ignored);
        std::this_thread::yield();
    } while (mesher.stats().allocatedBytes!=expectedAllocated && std::chrono::steady_clock::now()<deadline);
    require(mesher.stats().allocatedBytes==expectedAllocated,"Idle retirement did not reclaim its allocation");
    require(ignored.empty(),"Unexpected duplicate completion");
}
void checkDrawLists(GpuMesher& mesher,const MeshHandle& first,const MeshHandle& second)
{
    // A complete target/program and transform feedback exercise real direct
    // and indirect submissions under a surfaceless GL context.
    const char* vertex="#version 450 core\n#extension GL_ARB_shader_draw_parameters : require\n"
        "out uint capturedVertex;out uint capturedSlot;"
        "void main(){capturedVertex=uint(gl_VertexID);capturedSlot=uint(gl_BaseInstanceARB);"
        "gl_Position=vec4(float(gl_VertexID%2),float((gl_VertexID/2)%2),0,1);}";
    const char* fragment="#version 450 core\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}";
    const GLuint program=glCreateProgram();
    const GLenum stages[]={GL_VERTEX_SHADER,GL_FRAGMENT_SHADER};
    const char* sources[]={vertex,fragment};
    for (unsigned i=0;i<2;++i)
    {
        const GLuint shader=glCreateShader(stages[i]);
        glShaderSource(shader,1,&sources[i],nullptr);glCompileShader(shader);
        GLint compiled=GL_FALSE;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
        require(compiled==GL_TRUE,"Draw-list test shader failed");
        glAttachShader(program,shader);glDeleteShader(shader);
    }
    const char* capturedNames[]={"capturedVertex","capturedSlot"};
    glTransformFeedbackVaryings(program,2,capturedNames,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);
    GLint linked=GL_FALSE;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    require(linked==GL_TRUE,"Draw-list test program failed");
    GLuint framebuffer=0,color=0;
    glCreateFramebuffers(1,&framebuffer);glCreateRenderbuffers(1,&color);
    glNamedRenderbufferStorage(color,GL_RGBA8,1,1);
    glNamedFramebufferRenderbuffer(framebuffer,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
    require(glCheckNamedFramebufferStatus(framebuffer,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Draw-list test framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,1,1);glUseProgram(program);
    GLuint feedback=0,primitiveQuery=0;
    glGenQueries(1,&primitiveQuery);
    glCreateBuffers(1,&feedback);
    glNamedBufferStorage(feedback,(first.vertexCount+second.vertexCount)*2*sizeof(GLuint),nullptr,GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    auto checkCaptured=[&](const std::vector<MeshHandle>& handles)
    {
        std::size_t count=0;for(const auto& handle:handles)count+=handle.vertexCount;
        GLuint primitives=0;glGetQueryObjectuiv(primitiveQuery,GL_QUERY_RESULT,&primitives);
        require(primitives*3==count,"Draw submitted wrong vertex count");
        std::vector<GLuint> values(count*2);
        glGetNamedBufferSubData(feedback,0,values.size()*sizeof(GLuint),values.data());
        std::size_t index=0;
        for(const auto& handle:handles)for(unsigned i=0;i<handle.vertexCount;++i,++index)
            require(values[index*2]==handle.firstVertex+i && values[index*2+1]==handle.slot,
                "Draw submitted wrong vertex range or descriptor base instance");
    };
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,primitiveQuery);
    glBeginTransformFeedback(GL_TRIANGLES);mesher.draw(first);glEndTransformFeedback();
    glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    checkCaptured({first});
    const std::vector<MeshHandle> camera{first},shadow{second},changedShadow{first,second};
    require(mesher.stats().drawListUploads==0 && mesher.stats().drawListUploadBytes==0,"Initial draw cache is not empty");
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,primitiveQuery);
    glBeginTransformFeedback(GL_TRIANGLES);
    mesher.drawMany(camera,0);mesher.drawMany(shadow,1);
    glEndTransformFeedback();glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    checkCaptured({first,second});
    require(mesher.stats().drawListUploads==2 && mesher.stats().drawListUploadBytes==32,"First camera/shadow lists did not upload exactly once");
    for (unsigned frame=0;frame<8;++frame)
    {
        mesher.drawMany(camera,0);mesher.drawMany(shadow,1);
    }
    require(mesher.stats().drawListUploads==2 && mesher.stats().drawListUploadBytes==32,"Static interleaved passes reuploaded their draw lists");
    mesher.drawMany(changedShadow,1);mesher.drawMany(camera,0);
    require(mesher.stats().drawListUploads==3 && mesher.stats().drawListUploadBytes==64,"Changing one pass invalidated another pass's draw cache");
    mesher.drawMany({},0);mesher.drawMany(camera,0);
    require(mesher.stats().drawListUploads==3,"Empty pass invalidated an unchanged command buffer");
    require(glGetError()==GL_NO_ERROR,"OpenGL error during cached indirect draws");
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);glDeleteBuffers(1,&feedback);glDeleteQueries(1,&primitiveQuery);
    glUseProgram(0);glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteProgram(program);glDeleteFramebuffers(1,&framebuffer);glDeleteRenderbuffers(1,&color);
}

std::vector<std::array<std::uint32_t,3>> meshBytes(GpuMesher& mesher,const MeshHandle& mesh)
{
    std::vector<std::array<std::uint32_t,3>> vertices(mesh.vertexCount);
    glGetNamedBufferSubData(mesher.vertexBuffer(),static_cast<GLintptr>(mesh.firstVertex)*12,
        static_cast<GLsizeiptr>(vertices.size()*12),vertices.data());
    std::sort(vertices.begin(),vertices.end());
    return vertices;
}
// Intercept the real GL entry points after initialization: streaming/revisiting
// regions must never create a shader or link a program on this generic path.
unsigned streamingCompiles=0, streamingLinks=0;
PFNGLCOMPILESHADERPROC originalCompile=nullptr;
PFNGLLINKPROGRAMPROC originalLink=nullptr;
void GLAPIENTRY observeCompile(GLuint shader) { ++streamingCompiles; originalCompile(shader); }
void GLAPIENTRY observeLink(GLuint program) { ++streamingLinks; originalLink(program); }
struct CompileObserver
{
    CompileObserver()
    {
        streamingCompiles=streamingLinks=0;
        originalCompile=__glewCompileShader; originalLink=__glewLinkProgram;
        __glewCompileShader=observeCompile; __glewLinkProgram=observeLink;
    }
    ~CompileObserver() { __glewCompileShader=originalCompile; __glewLinkProgram=originalLink; }
};
unsigned timingGlCalls=0;
PFNGLGENQUERIESPROC originalGenQueries=nullptr;
PFNGLQUERYCOUNTERPROC originalQueryCounter=nullptr;
PFNGLGETQUERYOBJECTIVPROC originalGetQueryObjectiv=nullptr;
PFNGLGETQUERYOBJECTUI64VPROC originalGetQueryObjectui64v=nullptr;
void GLAPIENTRY observeGenQueries(GLsizei count,GLuint* queries)
{ ++timingGlCalls;originalGenQueries(count,queries); }
void GLAPIENTRY observeQueryCounter(GLuint query,GLenum target)
{ ++timingGlCalls;originalQueryCounter(query,target); }
void GLAPIENTRY observeGetQueryObjectiv(GLuint query,GLenum parameter,GLint* value)
{ ++timingGlCalls;originalGetQueryObjectiv(query,parameter,value); }
void GLAPIENTRY observeGetQueryObjectui64v(GLuint query,GLenum parameter,GLuint64* value)
{ ++timingGlCalls;originalGetQueryObjectui64v(query,parameter,value); }
struct TimingObserver
{
    TimingObserver()
    {
        timingGlCalls=0;
        originalGenQueries=__glewGenQueries;originalQueryCounter=__glewQueryCounter;
        originalGetQueryObjectiv=__glewGetQueryObjectiv;
        originalGetQueryObjectui64v=__glewGetQueryObjectui64v;
        __glewGenQueries=observeGenQueries;__glewQueryCounter=observeQueryCounter;
        __glewGetQueryObjectiv=observeGetQueryObjectiv;
        __glewGetQueryObjectui64v=observeGetQueryObjectui64v;
    }
    ~TimingObserver()
    {
        __glewGenQueries=originalGenQueries;__glewQueryCounter=originalQueryCounter;
        __glewGetQueryObjectiv=originalGetQueryObjectiv;
        __glewGetQueryObjectui64v=originalGetQueryObjectui64v;
    }
};
void checkGenericBiomes(GpuMesher& mesher,GpuConfig config)
{
    config.cells=8;config.maxInFlight=2;config.maxResidentChunks=4;config.vertexBudgetMiB=1;
    config.field.mode=FieldMode::Terrain;config.field.baseHeight=3.25f;
    config.field.heightAmplitude=0;config.field.detailScale=0;
    config.field.biomeOverride=-1;
    std::string error;
    require(mesher.initialize(config,error),error);
    const std::uint64_t side=config.cells+3;
    const auto minimumScratchBytes=config.maxInFlight*side*side*FieldScratchColumnBytes;
    GLint fieldAlignment=0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&fieldAlignment);
    const auto columnSlots=std::max(config.maxInFlight,config.columnCacheEntries);
    const auto densitySlots=std::max(config.maxInFlight,config.densityCacheEntries);
    const auto stateBytes=densitySlots*(1+((side+7)/8)*((side+7)/8))*16;
    const auto minimumCacheBytes=columnSlots*side*side*FieldColumnBytes+densitySlots*side*side*side*4+stateBytes;
    require(FieldColumnBytes==32 && FieldScratchColumnBytes==48,"Packed/floating field layout contract changed");
    require(mesher.stats().transientFieldBytes>=minimumScratchBytes &&
        mesher.stats().transientFieldBytes<=minimumScratchBytes+config.maxInFlight*2*fieldAlignment,
        "Transient accumulation exceeds one aligned FP32 scratch slice per in-flight job");
    require(mesher.stats().cachedFieldBytes>=minimumCacheBytes &&
        mesher.stats().cachedFieldBytes<=minimumCacheBytes+(columnSlots+densitySlots)*fieldAlignment,
        "Field caches exceed configured bounded capacities plus alignment");
    require(mesher.stats().biomeMetadataBytes==1024+52*(16+16*4)+config.maxInFlight*(16+48)+stateBytes,
        "Biome metadata no longer matches compact commands, job records and per-slot tile states");
    CompileObserver observer;
    ChunkRequest request;request.seed=1337;
    require(mesher.submit(request,100),mesher.lastError());
    mesher.cancel(100);
    const auto cancelled=finish(mesher,1).front();
    require(cancelled.status==CompletionStatus::Cancelled && mesher.stats().allocatedBytes==0,
        "Generic generation cancellation leaked a job/allocation");
    const auto first=one(mesher,request,101);
    require(first.status==CompletionStatus::Ready,"Automatic biome generation did not produce a mesh");
    const auto reference=meshBytes(mesher,first.mesh);
    mesher.release(first.mesh);collectRetired(mesher);
    const std::array<std::array<int,2>,5> origins{{{{4096,0}},{{-4096,4096}},{{8192,-8192}},{{-16384,8192}},{{24576,16384}}}};
    std::uint64_t token=102;
    for(const auto& origin:origins)
    {
        request.origin[0]=origin[0];request.origin[2]=origin[1];
        const auto mesh=one(mesher,request,token++);
        require(mesh.status==CompletionStatus::Ready,"Visiting another biome region failed");
        mesher.release(mesh.mesh);collectRetired(mesher);
    }
    request.origin={0,0,0};
    const auto revisited=one(mesher,request,token++);
    require(revisited.status==CompletionStatus::Ready && meshBytes(mesher,revisited.mesh)==reference,
        "Revisiting a region changed its packed positions/normals/climate");
    mesher.release(revisited.mesh);collectRetired(mesher);
    const auto idle=mesher.stats();
    std::vector<Completion> idleCompletions;
    for(unsigned i=0;i<64;++i)mesher.poll(idleCompletions);
    const auto afterIdle=mesher.stats();
    require(idleCompletions.empty() && afterIdle.pendingJobs==0 &&
        afterIdle.completedJobs==idle.completedJobs && afterIdle.emptyJobs==idle.emptyJobs &&
        afterIdle.failedJobs==idle.failedJobs && afterIdle.allocatedBytes==idle.allocatedBytes &&
        afterIdle.drawListUploads==idle.drawListUploads && afterIdle.fieldBatches==idle.fieldBatches,
        "An idle terrain continued scheduling generation or uploads");
    require(streamingCompiles==0 && streamingLinks==0,
        "Terrain streaming, region revisits or idle polling compiled/linked shaders");
    mesher.shutdown();
    std::cout<<"PASS biome backend: cancellation, region revisits, idle polling, no runtime shader compilation, identical packed meshes and bounded field caches\n";
}

void checkBatching(GpuMesher& mesher,GpuConfig config)
{
    config.cells=8;config.maxInFlight=4;config.maxResidentChunks=4;config.vertexBudgetMiB=1;
    config.field.mode=FieldMode::Terrain;config.field.baseHeight=3.25f;
    config.field.heightAmplitude=2.0f;config.field.heightFrequency=.1f;
    config.field.detailScale=.01f;
    config.field.biomeOverride=11;
    std::string error;
    require(mesher.initialize(config,error),error);
    const std::array<ChunkRequest,4> requests{{
        {{{-8,0,-8}},0,0,1337}, {{{0,0,-8}},0,0,17},
        {{{8,0,0}},0,0,98765}, {{{0,0,8}},0,0,17}
    }};
    std::vector<std::vector<std::array<std::uint32_t,3>>> reference;
    for(unsigned i=0;i<requests.size();++i)
    {
        const auto done=one(mesher,requests[i],200+i);
        require(done.status==CompletionStatus::Ready,"Sequential batch reference missed the surface");
        reference.push_back(meshBytes(mesher,done.mesh));
        mesher.release(done.mesh);collectRetired(mesher);
    }
    CompileObserver observer;
    const auto batches=mesher.stats().fieldBatches;
    for(unsigned i=0;i<requests.size();++i)
        require(mesher.submit(requests[i],300+i),mesher.lastError());
    require(mesher.stats().pendingJobs==4 && mesher.stats().fieldBatches==batches,
        "submit did not defer field generation for grouping ready chunks");
    mesher.dispatchQueued();
    require(mesher.stats().pendingJobs==4 && mesher.stats().fieldBatches==batches+1,
        "Explicit dispatch did not start the batch while preserving completions");
    mesher.dispatchQueued();
    require(mesher.stats().fieldBatches==batches+1,
        "Repeated explicit dispatch resubmitted already running jobs");
    const auto completed=finish(mesher,4);
    require(mesher.stats().fieldBatches==batches+1 && mesher.stats().lastFieldBatchJobs==4,
        "Four queued chunks were not grouped into one field batch");
    for(const auto& done:completed)
    {
        require(done.token>=300 && done.token<304 && done.status==CompletionStatus::Ready,
            "Batch completion lost its token or failed: "+done.error);
        require(meshBytes(mesher,done.mesh)==reference[done.token-300],
            "Batch changed a chunk's density, seed, normals or climate packing");
        mesher.release(done.mesh);
    }
    collectRetired(mesher);
    // Reuse all slots, including a cancelled queue entry, with different seeds
    // and origins. Stale indirect lists must not target the cancelled slot.
    for(unsigned i=0;i<requests.size();++i)
        require(mesher.submit(requests[3-i],400+i),mesher.lastError());
    mesher.cancel(401);
    const auto reused=finish(mesher,4);
    for(const auto& done:reused)
    {
        require(done.token>=400 && done.token<404,"Reused batch returned a stale token");
        if(done.token==401)
            require(done.status==CompletionStatus::Cancelled,"Queued cancellation was ignored");
        else
        {
            require(done.status==CompletionStatus::Ready &&
                meshBytes(mesher,done.mesh)==reference[3-(done.token-400)],
                "Reusing a batch slot changed its mesh or seed");
            mesher.release(done.mesh);
        }
    }
    collectRetired(mesher);
    const auto beforeCancelled=mesher.stats().fieldBatches;
    require(mesher.submit(requests[0],500),mesher.lastError());
    require(mesher.submit(requests[1],501),mesher.lastError());
    mesher.cancel(500);mesher.cancel(501);
    mesher.dispatchQueued();
    const auto fullyCancelled=finish(mesher,2);
    require(mesher.stats().fieldBatches==beforeCancelled &&
        fullyCancelled[0].status==CompletionStatus::Cancelled &&
        fullyCancelled[1].status==CompletionStatus::Cancelled,
        "A fully cancelled queued batch dispatched field work or lost completions");
    require(streamingCompiles==0 && streamingLinks==0,"Batch/reuse compiled shaders at runtime");
    mesher.shutdown();
    std::cout<<"PASS batching: four chunks in one GPU batch; distinct seeds match isolated jobs, slot reuse/cancellation and no runtime compilation\n";
}

struct MeshSnapshot
{
    CompletionStatus status=CompletionStatus::Failed;
    std::vector<std::array<std::uint32_t,9>> triangles;
};
std::vector<std::array<std::uint32_t,9>> meshTriangles(GpuMesher& mesher,const MeshHandle& mesh)
{
    require(mesh.vertexCount%3==0,"Cached mesh does not contain complete triangles");
    std::vector<std::array<std::uint32_t,9>> triangles(mesh.vertexCount/3);
    glGetNamedBufferSubData(mesher.vertexBuffer(),static_cast<GLintptr>(mesh.firstVertex)*12,
        static_cast<GLsizeiptr>(triangles.size()*36),triangles.data());
    std::sort(triangles.begin(),triangles.end());
    return triangles;
}
MeshSnapshot snapshot(GpuMesher& mesher,const Completion& done)
{
    require(done.status==CompletionStatus::Ready || done.status==CompletionStatus::Empty,
        "Cache fixture generation failed: "+done.error);
    MeshSnapshot result;result.status=done.status;
    if(done.status==CompletionStatus::Ready)
    {
        result.triangles=meshTriangles(mesher,done.mesh);
        mesher.release(done.mesh);
    }
    return result;
}
void matches(GpuMesher& mesher,const Completion& done,const MeshSnapshot& reference)
{
    const auto actual=snapshot(mesher,done);
    require(actual.status==reference.status && actual.triangles==reference.triangles,
        "Field cache changed packed triangles, normals, climate or empty classification");
}

void checkFieldReuse(GpuMesher& mesher,GpuConfig config)
{
    config.cells=8;config.maxInFlight=4;config.maxResidentChunks=8;config.vertexBudgetMiB=4;
    config.columnCacheEntries=0;config.densityCacheEntries=0;
    config.field.mode=FieldMode::Terrain;config.field.baseHeight=3.25f;
    config.field.heightAmplitude=.5f;config.field.heightFrequency=.05f;
    config.field.detailScale=.03f;
    config.field.biomeOverride=11;
    const std::array<ChunkRequest,9> requests{{
        {{{0,0,0}},0,0,1337}, {{{0,0,0}},0,21,1337},
        {{{0,8,0}},0,0,1337}, {{{0,0,0}},0,0,17},
        {{{0,0,0}},1,0,1337}, {{{8,0,0}},0,0,1337},
        {{{16,0,0}},0,0,1337}, {{{24,0,0}},0,0,1337},
        {{{0,1024,0}},0,0,1337}
    }};
    std::string error;
    require(mesher.initialize(config,error),error);
    std::vector<MeshSnapshot> reference;
    for(unsigned i=0;i<requests.size();++i)
    {
        reference.push_back(snapshot(mesher,one(mesher,requests[i],1000+i)));
        collectRetired(mesher);
    }
    require(reference[0].status==CompletionStatus::Ready && reference[1].status==CompletionStatus::Ready,
        "Cache reference did not include real terrain and a transition mesh");
    require(reference[8].status==CompletionStatus::Empty,"Homogeneous cache reference unexpectedly crossed terrain");
    require(mesher.stats().generatedColumnTiles==requests.size() &&
        mesher.stats().generatedDensityFields==requests.size() &&
        mesher.stats().columnCacheHits==0 && mesher.stats().densityCacheHits==0,
        "Zero retention reused a completed column or density field");

    config.columnCacheEntries=config.densityCacheEntries=4;
    require(mesher.initialize(config,error),error);
    for(unsigned i=0;i<3;++i) require(mesher.submit(requests[i],1100+i),mesher.lastError());
    mesher.dispatchQueued();
    require(mesher.stats().generatedColumnTiles==1 && mesher.stats().generatedDensityFields==2 &&
        mesher.stats().columnCacheHits==2 && mesher.stats().densityCacheHits==1,
        "Same-batch vertical tiles or mask aliases were generated more than once");
    for(const auto& done:finish(mesher,3))
    {
        require(done.token>=1100 && done.token<1103,"Alias batch returned a stale token");
        matches(mesher,done,reference[done.token-1100]);
    }
    collectRetired(mesher);
    const auto beforeMask=mesher.stats();
    matches(mesher,one(mesher,requests[1],1110),reference[1]);collectRetired(mesher);
    require(mesher.stats().generatedColumnTiles==beforeMask.generatedColumnTiles &&
        mesher.stats().generatedDensityFields==beforeMask.generatedDensityFields &&
        mesher.stats().densityCacheHits==beforeMask.densityCacheHits+1,
        "Mask-only rebuild regenerated a cached field");
    for(unsigned i:{3u,4u})
    {
        const auto before=mesher.stats();
        matches(mesher,one(mesher,requests[i],1120+i),reference[i]);collectRetired(mesher);
        require(mesher.stats().generatedColumnTiles==before.generatedColumnTiles+1 &&
            mesher.stats().generatedDensityFields==before.generatedDensityFields+1,
            "Seed or LOD change aliased an incompatible cached field");
    }

    // A cancelled owner must not invalidate data needed by an alias. Exercise
    // both cancellation before ownership acquisition and after GPU submission.
    config.maxInFlight=2;config.columnCacheEntries=config.densityCacheEntries=2;
    for(bool submitted:{false,true})
    {
        require(mesher.initialize(config,error),error);
        require(mesher.submit(requests[0],1200),mesher.lastError());
        require(mesher.submit(requests[1],1201),mesher.lastError());
        if(submitted) mesher.dispatchQueued();
        mesher.cancel(1200);
        for(const auto& done:finish(mesher,2))
        {
            if(done.token==1200) require(done.status==CompletionStatus::Cancelled,"Owner cancellation ignored");
            else { require(done.token==1201,"Cancelled alias returned wrong token");matches(mesher,done,reference[1]); }
        }
        collectRetired(mesher);
        require(mesher.stats().generatedColumnTiles==1 && mesher.stats().generatedDensityFields==1,
            "Cancelled owner caused missing or duplicate field construction");
        const auto before=mesher.stats();
        matches(mesher,one(mesher,requests[0],1202),reference[0]);collectRetired(mesher);
        require(mesher.stats().generatedDensityFields==before.generatedDensityFields,
            "Surviving alias did not leave a reusable completed field");
    }

    require(mesher.initialize(config,error),error);
    require(mesher.submit(requests[0],1300),mesher.lastError());mesher.dispatchQueued();
    require(mesher.submit(requests[5],1301),mesher.lastError());mesher.dispatchQueued();
    require(!mesher.submit(requests[6],1302),"Third job bypassed the pinned two-job budget");
    for(const auto& done:finish(mesher,2)) matches(mesher,done,reference[done.token==1300?0:5]);
    collectRetired(mesher);
    auto before=mesher.stats();
    matches(mesher,one(mesher,requests[6],1303),reference[6]);collectRetired(mesher);
    require(mesher.stats().generatedDensityFields==before.generatedDensityFields+1,"New key did not replace an LRU entry");
    before=mesher.stats();
    matches(mesher,one(mesher,requests[5],1304),reference[5]);collectRetired(mesher);
    require(mesher.stats().generatedDensityFields==before.generatedDensityFields,"LRU evicted the more recently used field");
    before=mesher.stats();
    matches(mesher,one(mesher,requests[0],1305),reference[0]);collectRetired(mesher);
    require(mesher.stats().generatedDensityFields==before.generatedDensityFields+1,"Evicted field was not regenerated on revisit");

    require(mesher.initialize(config,error),error);
    matches(mesher,one(mesher,requests[8],1400),reference[8]);collectRetired(mesher);
    auto empty=requests[8];empty.transitionMask=63;
    matches(mesher,one(mesher,empty,1401),reference[8]);collectRetired(mesher);
    require(mesher.stats().generatedColumnTiles==1 && mesher.stats().generatedDensityFields==1 &&
        mesher.stats().columnCacheHits==1 && mesher.stats().densityCacheHits==1,
        "Homogeneous cached field read unwritten density or regenerated for a mask change");

    config.columnCacheEntries=config.densityCacheEntries=0;
    require(mesher.initialize(config,error),error);
    for(unsigned i=0;i<2;++i) require(mesher.submit(requests[i],1500+i),mesher.lastError());
    for(const auto& done:finish(mesher,2)) matches(mesher,done,reference[done.token-1500]);
    collectRetired(mesher);
    require(mesher.stats().generatedColumnTiles==1 && mesher.stats().generatedDensityFields==1,
        "Zero completed retention prevented safe in-flight sharing");
    matches(mesher,one(mesher,requests[0],1502),reference[0]);collectRetired(mesher);
    require(mesher.stats().generatedColumnTiles==2 && mesher.stats().generatedDensityFields==2,
        "Zero retention kept an alias field after its last consumer completed");
    mesher.shutdown();
    std::cout<<"PASS GPU field caches: exact uncached triangles; vertical XZ reuse, mask-only rebuilds, seed/LOD keys, same-batch aliases, cancelled owners, pinned LRU, homogeneous fields and zero retention\n";
}

void checkIdleControls(GpuMesher& mesher,GpuConfig config)
{
    config.cells=8;config.maxInFlight=2;config.maxResidentChunks=4;config.vertexBudgetMiB=1;
    config.columnCacheEntries=config.densityCacheEntries=4;
    config.field.mode=FieldMode::Terrain;config.field.baseHeight=3.25f;
    config.field.heightAmplitude=.5f;config.field.heightFrequency=.05f;
    config.field.detailScale=.03f;config.field.biomeOverride=11;
    std::string error;
    require(!mesher.clearFieldCaches(),"Uninitialized mesher accepted cache invalidation");
    require(mesher.initialize(config,error),error);
    ChunkRequest request;request.seed=1337;
    require(mesher.submit(request,1600),mesher.lastError());
    require(!mesher.clearFieldCaches(),"Cache invalidation accepted queued work");
    mesher.dispatchQueued();
    require(!mesher.clearFieldCaches(),"Cache invalidation accepted submitted work");
    const auto first=finish(mesher,1).front();
    require(first.status==CompletionStatus::Ready,"Idle invalidation fixture did not produce a surface");
    const auto reference=meshTriangles(mesher,first.mesh);
    const auto before=mesher.stats();
    const auto arena=mesher.vertexBuffer();
    require(mesher.clearFieldCaches(),"Idle cache invalidation failed");
    require(mesher.valid(first.mesh) && mesher.vertexBuffer()==arena &&
        mesher.stats().residentChunks==before.residentChunks &&
        mesher.stats().allocatedBytes==before.allocatedBytes &&
        meshTriangles(mesher,first.mesh)==reference,
        "Clearing fields invalidated or changed a published mesh/allocation");
    const auto second=one(mesher,request,1601);
    require(second.status==CompletionStatus::Ready && meshTriangles(mesher,second.mesh)==reference &&
        mesher.valid(first.mesh),"Regeneration after cache invalidation changed published geometry");
    require(mesher.stats().generatedColumnTiles==before.generatedColumnTiles+1 &&
        mesher.stats().generatedDensityFields==before.generatedDensityFields+1,
        "Idle invalidation did not force a new 2D and density field");
    mesher.release(first.mesh);mesher.release(second.mesh);collectRetired(mesher);
    mesher.shutdown();

    config.measureGenerationTimings=false;
    {
        TimingObserver observer;
        require(mesher.initialize(config,error),error);
        const auto untimed=one(mesher,request,1610);
        require(untimed.status==CompletionStatus::Ready && meshTriangles(mesher,untimed.mesh)==reference,
            "Disabling timings changed generation");
        mesher.release(untimed.mesh);collectRetired(mesher);
        std::vector<Completion> ignored;
        for(unsigned i=0;i<8;++i) mesher.poll(ignored);
        const auto stats=mesher.stats();
        require(ignored.empty() && stats.pendingTimingSamples==0 && stats.droppedTimingSamples==0 &&
            stats.timedBatches==0 && stats.timedEmissions==0 && timingGlCalls==0,
            "Disabled timing created, submitted or read GPU queries");
        require(glGetError()==GL_NO_ERROR,"Disabling timings produced an OpenGL error");
        mesher.shutdown();
    }
    std::cout<<"PASS idle controls: cache invalidation rejects pending work, preserves published meshes and forces rebuild; disabled timing issues zero GL queries\n";
}

}

extern "C" int terrain_backend_tests(const char* shaderDirectory)
{
    try
    {
        glewExperimental=GL_TRUE;
        const auto glewStatus=glewInit();
        // GLEW compiled with GLX can return NO_GLX_DISPLAY after successfully
        // loading desktop GL functions in the current EGL context.
        require(glewStatus==GLEW_OK || glewStatus==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        require(GLEW_VERSION_4_5 && glCreateBuffers,"Backend requires desktop GL4.5 DSA");
        while (glGetError()!=GL_NO_ERROR) {}
        GpuMesher mesher;
        GpuConfig config;
        config.cells=8;config.maxInFlight=2;config.maxResidentChunks=2;config.vertexBudgetMiB=1;
        config.shaderDirectory=shaderDirectory;config.field.mode=FieldMode::Plane;config.field.baseHeight=3.25f;
        std::string error;
        require(mesher.initialize(config,error),error);
        ChunkRequest a,b,c;
        b.origin[0]=8;c.origin[0]=16;
        require(mesher.submit(a,1),mesher.lastError());
        require(!mesher.submit(a,1),"Duplicate token accepted");
        require(mesher.submit(b,2),mesher.lastError());
        require(!mesher.submit(c,3),"In-flight limit ignored");
        mesher.cancel(2);
        const auto completed=finish(mesher,2);
        MeshHandle first;
        for (const auto& done:completed)
        {
            if (done.token==1)
            {
                require(done.status==CompletionStatus::Ready,"First plane job failed: "+done.error);
                first=done.mesh;
            }
            else require(done.token==2 && done.status==CompletionStatus::Cancelled,"Cancellation lost token/status");
        }
        require(mesher.valid(first) && first.vertexCount==384,"Ready handle/count invalid");
        require(mesher.stats().pendingJobs==0 && mesher.stats().residentChunks==1,"Pending/resident accounting");
        auto second=one(mesher,b,3);
        require(second.status==CompletionStatus::Ready,"Second resident plane failed");
        checkDrawLists(mesher,first,second.mesh);
        const auto allocated=mesher.stats().allocatedBytes;
        auto overflow=one(mesher,c,4);
        require(overflow.status==CompletionStatus::Failed && !overflow.error.empty(),"Resident slot overflow not reported");
        require(mesher.stats().allocatedBytes==allocated && mesher.stats().residentChunks==2,"Failed job leaked allocation");
        mesher.release(first);
        mesher.release(first); // Idempotent, including before retirement fence.
        require(!mesher.valid(first) && mesher.stats().residentChunks==1,"Released mesh remained valid");
        collectRetired(mesher,allocated/2);
        auto replacement=one(mesher,c,5);
        require(replacement.status==CompletionStatus::Ready && mesher.valid(replacement.mesh),"Released slot not reusable");
        require(!mesher.valid(first) && replacement.mesh.generation!=first.generation,"Stale handle alias after reuse");
        mesher.release(second.mesh);mesher.release(replacement.mesh);
        collectRetired(mesher);
        require(mesher.stats().allocatedBytes==0 && mesher.stats().residentVertices==0,"Released vertex allocation leaked");
        require(mesher.submit(a,6),mesher.lastError());
        std::vector<Completion> pending;
        // Submit without polling: a cached field can finish count before the
        // very first poll, so poll->glFinish->poll may already publish its emit.
        mesher.dispatchQueued();
        glFinish();
        mesher.poll(pending); // Count complete, starts emit phase.
        require(pending.empty(),"Count phase unexpectedly published a mesh");
        require(mesher.stats().allocatedBytes>0,"Cancellation fixture did not reach mesh allocation");
        mesher.cancel(6);
        auto cancelled=finish(mesher,1).front();
        require(cancelled.status==CompletionStatus::Cancelled && mesher.stats().allocatedBytes==0,"Emit cancellation leaked its reserved slot");
        ChunkRequest empty=a;empty.origin[1]=16;
        require(one(mesher,empty,7).status==CompletionStatus::Empty,"Empty chunk did not produce Empty completion");
        ChunkRequest invalid=a;invalid.origin[0]=1;
        require(!mesher.submit(invalid,8),"Unaligned chunk accepted");
        auto old=one(mesher,a,9).mesh;
        require(mesher.initialize(config,error),error);
        require(mesher.stats().drawListUploads==0 && mesher.stats().drawListUploadBytes==0,"Restart retained draw-cache statistics");
        auto fresh=one(mesher,a,10).mesh;
        require(mesher.valid(fresh) && !mesher.valid(old),"Stale handle alias across shutdown/reinitialize");
        require(mesher.submit(b,11),mesher.lastError());
        mesher.shutdown(); // Must be safe with outstanding GL work.
        require(!mesher.valid(fresh) && mesher.stats().allocatedBytes==0,"Shutdown retained a live allocation");
        require(mesher.initialize(config,error),error);
        auto restored=one(mesher,a,12);
        require(restored.status==CompletionStatus::Ready,"Backend could not restart after in-flight shutdown");
        config.cells=64;config.maxResidentChunks=16;
        require(mesher.initialize(config,error),error);
        for (unsigned i=0;i<4;++i)
        {
            ChunkRequest request;request.origin[0]=i*64;
            auto done=one(mesher,request,20+i);
            require(done.status==(i<3?CompletionStatus::Ready:CompletionStatus::Failed),"Vertex memory budget not enforced");
            require(mesher.stats().allocatedBytes<=mesher.stats().budgetBytes,"Allocated more than the fixed vertex budget");
        }
        require(mesher.stats().residentChunks==3 && mesher.stats().failedJobs==1,"Vertex budget failure leaked a resident slot");
        std::cout << "PASS backend: async phases, cancellation, slot budget, fence retirement, stale handles, empty chunks and restart\n";
        std::cout << "PASS backend draw cache: camera/shadow passes upload once; static frames upload zero command bytes\n";
        std::cout << "PASS backend vertex budget: three 64³ planes fit, fourth fails without exceeding its allocation\n";
        std::cout << "Software-driver GPU query for last 64³ plane: " << mesher.stats().lastGenerationMilliseconds << " ms (not hardware performance)\n";
        mesher.shutdown();
        checkGenericBiomes(mesher,config);
        checkBatching(mesher,config);
        checkFieldReuse(mesher,config);
        checkIdleControls(mesher,config);
        require(glGetError()==GL_NO_ERROR,"OpenGL error after shutdown");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL backend: " << error.what() << '\n';
        return 1;
    }
}
