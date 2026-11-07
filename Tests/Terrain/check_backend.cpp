// Loaded by check_backend.py after creating an EGL context. It exercises the
// production C++ scheduler/allocation lifecycle, not a duplicate implementation.
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "SharedEglContext.hpp"
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
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (completed.size()<expected && std::chrono::steady_clock::now()<deadline)
    {
        mesher.poll(completed);
        std::this_thread::yield();
    }
    require(completed.size()==expected,"Async backend did not complete jobs within 10s: "+mesher.lastError());
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
    // An actual complete target/program exercises the production indirect
    // submission path under a surfaceless GL context.
    const char* vertex="#version 450 core\nvoid main(){gl_Position=vec4(float(gl_VertexID%2),float((gl_VertexID/2)%2),0,1);}";
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
    glLinkProgram(program);
    GLint linked=GL_FALSE;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    require(linked==GL_TRUE,"Draw-list test program failed");
    GLuint framebuffer=0,color=0;
    glCreateFramebuffers(1,&framebuffer);glCreateRenderbuffers(1,&color);
    glNamedRenderbufferStorage(color,GL_RGBA8,1,1);
    glNamedFramebufferRenderbuffer(framebuffer,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
    require(glCheckNamedFramebufferStatus(framebuffer,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Draw-list test framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,1,1);glUseProgram(program);
    const std::vector<MeshHandle> camera{first},shadow{second},changedShadow{first,second};
    require(mesher.stats().drawListUploads==0 && mesher.stats().drawListUploadBytes==0,"Initial draw cache is not empty");
    mesher.drawMany(camera,0);mesher.drawMany(shadow,1);
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
void finishCompiles(GpuMesher& mesher)
{
    std::vector<Completion> ignored;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(mesher.stats().pendingBiomeVariants && std::chrono::steady_clock::now()<deadline)
    {
        mesher.poll(ignored);
        std::this_thread::yield();
    }
    require(ignored.empty(),"Idle shader compilation produced a duplicate mesh completion");
    require(!mesher.stats().pendingBiomeVariants,"Parallel biome compilation failed to complete while no mesh jobs were pending");
    require(!mesher.stats().biomeCompileFailures,"Biome variant compilation failed: "+mesher.lastError());
}
void checkBiomeVariants(GpuMesher& mesher,GpuConfig config)
{
    config.cells=8;config.maxInFlight=2;config.maxResidentChunks=4;config.vertexBudgetMiB=1;
    config.maxBiomeVariants=2;config.maxParallelBiomeCompiles=1;
    config.field.mode=FieldMode::Terrain;config.field.baseHeight=3.25f;
    config.field.heightAmplitude=0;config.field.detailScale=0;
    config.field.biomeOverride=-1;
    std::string error;
    require(mesher.initialize(config,error),error);
    const std::uint64_t side=config.cells+3;
    require(mesher.stats().transientFieldBytes==config.maxInFlight*(side*side*side*4+side*side*FieldColumnBytes),
        "Transient fields no longer use the expected 48-byte columns and float density");
    ChunkRequest request;request.seed=1337;
    require(mesher.submit(request,100),mesher.lastError());
    mesher.cancel(100); // Cancel while the CPU is waiting for classification.
    const auto cancelled=finish(mesher,1).front();
    require(cancelled.status==CompletionStatus::Cancelled && mesher.stats().allocatedBytes==0,
        "Classification cancellation leaked a job/allocation");
    const auto first=one(mesher,request,101);
    require(first.status==CompletionStatus::Ready,"Automatic terrain fallback did not produce a mesh");
    const auto reference=meshBytes(mesher,first.mesh);
    const bool parallel=mesher.stats().parallelShaderCompilation;
    if(parallel) require(mesher.stats().biomeVariantFallbacks>0,"First unseen biome mask did not use generic fallback");
    mesher.release(first.mesh);collectRetired(mesher);
    finishCompiles(mesher);
    const auto hits=mesher.stats().biomeVariantHits;
    const auto second=one(mesher,request,102);
    require(second.status==CompletionStatus::Ready && meshBytes(mesher,second.mesh)==reference,
        "Cached specialized program changes actual packed geometry/normals/climate");
    if(parallel)require(mesher.stats().biomeVariantHits==hits+1,"Ready biome variant was not reused");
    mesher.release(second.mesh);collectRetired(mesher);
    const std::array<std::array<int,2>,5> origins{{{{4096,0}},{{-4096,4096}},{{8192,-8192}},{{-16384,8192}},{{24576,16384}}}};
    std::uint64_t token=103;
    for(const auto& origin:origins)
    {
        request.origin[0]=origin[0];request.origin[2]=origin[1];
        const auto mesh=one(mesher,request,token++);
        require(mesh.status==CompletionStatus::Ready,"Variant cache pressure interrupted terrain generation");
        require(mesher.stats().biomeVariants<=config.maxBiomeVariants &&
                mesher.stats().pendingBiomeVariants<=config.maxParallelBiomeCompiles,"Variant cache or compile budget exceeded");
        mesher.release(mesh.mesh);collectRetired(mesher);finishCompiles(mesher);
    }
    if(parallel) require(mesher.stats().biomeVariantFallbacks>=3 && mesher.stats().biomeVariants==2,
        "Cache test did not exercise more masks than its bounded capacity");
    const auto saturated=mesher.stats();
    if(parallel)require(saturated.biomeVariantCompiles==2 && saturated.biomeVariantCacheFull>0,
        "A full cache evicted/recompiled variants instead of retaining its two admitted programs");
    request.origin={0,0,0};
    const auto revisited=one(mesher,request,190);
    require(revisited.status==CompletionStatus::Ready && meshBytes(mesher,revisited.mesh)==reference,
        "Revisiting the first chunk changed its packed geometry after cache saturation");
    if(parallel) require(mesher.stats().biomeVariantHits==saturated.biomeVariantHits+1 &&
        mesher.stats().biomeVariantCompiles==saturated.biomeVariantCompiles,
        "The first variant was evicted or recompiled after visiting more biome masks");
    mesher.release(revisited.mesh);collectRetired(mesher);
    const auto idle=mesher.stats();
    std::vector<Completion> idleCompletions;
    for(unsigned i=0;i<64;++i)mesher.poll(idleCompletions);
    const auto afterIdle=mesher.stats();
    require(idleCompletions.empty() && afterIdle.pendingJobs==0 &&
        afterIdle.biomeVariants==idle.biomeVariants && afterIdle.pendingBiomeVariants==0 &&
        afterIdle.biomeVariantHits==idle.biomeVariantHits &&
        afterIdle.biomeVariantFallbacks==idle.biomeVariantFallbacks &&
        afterIdle.biomeVariantCompiles==idle.biomeVariantCompiles &&
        afterIdle.biomeVariantCacheFull==idle.biomeVariantCacheFull &&
        afterIdle.biomeCompileFailures==idle.biomeCompileFailures,
        "An idle terrain continued scheduling generation or shader work");
    // Generic-only mode must preserve the same world, and never enqueue a
    // runtime compilation even on a driver exposing parallel shader support.
    config.enableBiomeVariants=false;
    require(mesher.initialize(config,error),error);
    request.origin={0,0,0};
    const auto generic=one(mesher,request,200);
    require(generic.status==CompletionStatus::Ready && meshBytes(mesher,generic.mesh)==reference,
        "Disabling specialization changed terrain output");
    require(!mesher.stats().biomeVariants && !mesher.stats().pendingBiomeVariants,
        "Generic-only mode still allocated variants");
    config.enableBiomeVariants=true;
    config.shaderCompilerContext={};
    require(mesher.initialize(config,error),error);
    const auto noContext=one(mesher,request,201);
    require(noContext.status==CompletionStatus::Ready && meshBytes(mesher,noContext.mesh)==reference,
        "No-worker-context fallback changed terrain output");
    require(!mesher.stats().parallelShaderCompilation && !mesher.stats().biomeVariants &&
            !mesher.stats().pendingBiomeVariants,"Missing shared context caused render-thread compilation");
    mesher.shutdown();
    std::cout<<"PASS biome backend: classification cancellation, generic fallback, shared-context compilation, idle polling, stable full cache without recompilation, identical packed meshes and 48-byte columns\n";
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
        SharedEglContext compilerContext;
        GpuMesher mesher;
        GpuConfig config;
        config.shaderCompilerContext=compilerContext.hooks();
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
        GLuint command[4]{};
        glGetNamedBufferSubData(mesher.indirectBuffer(),first.slot*16,16,command);
        require(command[0]==384 && command[1]==1 && command[2]==first.firstVertex && command[3]==first.slot,"Indirect command disagrees with handle");
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
        glFinish();
        std::vector<Completion> pending;
        mesher.poll(pending); // Count complete, starts emit phase.
        require(pending.empty(),"Count phase unexpectedly published a mesh");
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
        checkBiomeVariants(mesher,config);
        require(glGetError()==GL_NO_ERROR,"OpenGL error after shutdown");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL backend: " << error.what() << '\n';
        return 1;
    }
}
