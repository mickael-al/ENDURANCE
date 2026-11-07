// Loaded by check_backend.py after creating an EGL context. It exercises the
// production C++ scheduler/allocation lifecycle, not a duplicate implementation.
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
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
void collectRetired(GpuMesher& mesher)
{
    // This explicit test-only completion allows precise allocator accounting.
    glFinish();
    std::vector<Completion> ignored;
    mesher.poll(ignored);
    require(ignored.empty(),"Unexpected duplicate completion");
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
        GLuint command[4]{};
        glGetNamedBufferSubData(mesher.indirectBuffer(),first.slot*16,16,command);
        require(command[0]==384 && command[1]==1 && command[2]==first.firstVertex && command[3]==first.slot,"Indirect command disagrees with handle");
        auto second=one(mesher,b,3);
        require(second.status==CompletionStatus::Ready,"Second resident plane failed");
        const auto allocated=mesher.stats().allocatedBytes;
        auto overflow=one(mesher,c,4);
        require(overflow.status==CompletionStatus::Failed && !overflow.error.empty(),"Resident slot overflow not reported");
        require(mesher.stats().allocatedBytes==allocated && mesher.stats().residentChunks==2,"Failed job leaked allocation");
        mesher.release(first);
        mesher.release(first); // Idempotent, including before retirement fence.
        require(!mesher.valid(first) && mesher.stats().residentChunks==1,"Released mesh remained valid");
        collectRetired(mesher);
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
        std::cout << "PASS backend vertex budget: three 64³ planes fit, fourth fails without exceeding its allocation\n";
        std::cout << "Software-driver GPU query for last 64³ plane: " << mesher.stats().lastGenerationMilliseconds << " ms (not hardware performance)\n";
        mesher.shutdown();
        require(glGetError()==GL_NO_ERROR,"OpenGL error after shutdown");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL backend: " << error.what() << '\n';
        return 1;
    }
}
