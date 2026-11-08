#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "ProceduralTerrainBiomes.hpp"
#include "ProceduralTransvoxelTables.hpp"
#include "ComputeShader.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

namespace ProceduralTerrain
{
    namespace
    {
        constexpr std::uint32_t VertexBytes = 12;
        struct Counters
        {
            std::uint32_t total, cursor, overflow, padding;
        };
        struct Command { std::uint32_t count, instances, first, baseInstance; };
        struct Descriptor { float originExtent[4]; std::uint32_t metadata[4]; };
        static_assert(sizeof(Counters) == 16 && sizeof(Command) == 16 && sizeof(Descriptor) == 32,
            "GPU structures must match std430 layout");

        struct Program
        {
            GLuint id = 0;
            GLint cells=-1, origin=-1, lod=-1, spacing=-1, seed=-1, mask=-1, parity=-1;
            GLint vertexBase=-1, capacity=-1;
            std::uint32_t cellCount=64;
            void discover()
            {
                cells=glGetUniformLocation(id,"uCells"); origin=glGetUniformLocation(id,"uOrigin");
                lod=glGetUniformLocation(id,"uLod"); spacing=glGetUniformLocation(id,"uBaseSpacing");
                seed=glGetUniformLocation(id,"uSeed"); mask=glGetUniformLocation(id,"uTransitionMask");
                parity=glGetUniformLocation(id,"uChunkParity");
                vertexBase=glGetUniformLocation(id,"uVertexBase"); capacity=glGetUniformLocation(id,"uVertexCapacity");
            }
            void field(const GpuConfig& config)
            {
                glUseProgram(id);
                cellCount=config.cells;
                if (vertexBase>=0) glUniform1ui(vertexBase,0);
                glUniform1ui(cells,config.cells); glUniform1f(spacing,config.baseSpacing);
                const auto& f=config.field;
                const auto set=[&](const char* name,float value){ glUniform1f(glGetUniformLocation(id,name),value); };
                set("uBaseHeight",f.baseHeight); set("uHeightAmplitude",f.heightAmplitude);
                set("uHeightFrequency",f.heightFrequency); set("uClimateFrequency",f.climateFrequency);
                set("uBandOffset",f.bandOffset); set("uBandOffsetVariation",f.bandOffsetVariation);
                set("uDetailScale",f.detailScale); set("uSphereRadius",f.sphereRadius);
                set("uSeaLevel",f.seaLevel); set("uSeaBlendWidth",f.seaBlendWidth);
                glUniform1i(glGetUniformLocation(id,"uBiomeOverride"),f.biomeOverride);
                glUniform1ui(glGetUniformLocation(id,"uFieldMode"),static_cast<std::uint32_t>(f.mode));
                glUniform3fv(glGetUniformLocation(id,"uSphereCenter"),1,f.sphereCenter.data());
            }
            void request(const ChunkRequest& request, std::uint32_t vertexCapacity=0) const
            {
                glUseProgram(id);
                if (origin>=0) glUniform3iv(origin,1,request.origin.data());
                if (lod>=0) glUniform1ui(lod,request.lod);
                if (seed>=0) glUniform1ui(seed,request.seed);
                if (mask>=0) glUniform1ui(mask,request.transitionMask);
                if (parity>=0)
                {
                    const std::int64_t span=static_cast<std::int64_t>(cellCount)*(std::int64_t{1}<<request.lod);
                    glUniform3ui(parity,static_cast<std::uint32_t>(request.origin[0]/span)&1u,
                        static_cast<std::uint32_t>(request.origin[1]/span)&1u,
                        static_cast<std::uint32_t>(request.origin[2]/span)&1u);
                }
                if (capacity>=0) glUniform1ui(capacity,vertexCapacity);
            }
        };

        enum ProgramIndex { Columns, Density, CountRegular, CountTransition, EmitRegular, EmitTransition, ProgramCount };
        enum class JobStage { Free, Counting, Emitting };
    }

    struct GpuMesher::Impl
    {
        struct Job
        {
            GLuint density=0, columns=0, counter=0;
            const volatile Counters* mapped=nullptr;
            GLuint timers[2]{};
            GLsync fence=nullptr;
            JobStage stage=JobStage::Free;
            std::uint64_t token=0;
            ChunkRequest request;
            MeshHandle mesh;
            bool cancelled=false;
        };
        struct Slot
        {
            std::uint32_t generation=0, first=0, allocation=0;
            bool live=false, reserved=false;
            MeshHandle mesh;
        };
        struct Retirement { std::uint32_t slot; GLsync fence; };
        struct DrawList
        {
            GLuint buffer=0;
            std::vector<Command> commands;
        };
        GpuConfig config;
        GpuStats statistics;
        std::string error;
        bool initialized=false, broken=false, flushPending=false;
        Program programs[ProgramCount];
        GLuint vertices=0, vao=0, tables=0, descriptors=0, biomeCells=0;
        std::vector<Job> jobs;
        std::vector<Slot> slots;
        std::vector<std::uint32_t> freeSlots;
        std::vector<Retirement> retired;
        std::map<std::uint32_t,std::uint32_t> freeRanges;
        std::map<std::uint32_t,DrawList> drawLists;
        std::vector<Command> drawScratch;
        std::uint32_t vertexAlignment=1;
        // Deliberately survives shutdown/reinitialize, unlike the slot array.
        std::uint32_t nextGeneration=0;
        std::uint64_t maximumStorageBlock=0;

        bool allocate(std::uint32_t count, MeshHandle& mesh, const ChunkRequest& request)
        {
            if (freeSlots.empty()) { error="Terrain resident chunk budget exhausted"; return false; }
            const std::uint64_t rounded64=(static_cast<std::uint64_t>(count)+vertexAlignment-1)/vertexAlignment*vertexAlignment;
            if (rounded64>UINT32_MAX || rounded64*VertexBytes>maximumStorageBlock)
            { error="Terrain chunk exceeds GPU storage block limit"; return false; }
            const auto rounded=static_cast<std::uint32_t>(rounded64);
            auto range=std::find_if(freeRanges.begin(),freeRanges.end(),[&](const auto& item){return item.second>=rounded;});
            if (range==freeRanges.end()) { error="Terrain vertex memory budget exhausted"; return false; }
            const auto first=range->first, available=range->second;
            freeRanges.erase(range);
            if (available>rounded) freeRanges.emplace(first+rounded,available-rounded);
            const auto slotIndex=freeSlots.back(); freeSlots.pop_back();
            auto& slot=slots[slotIndex];
            if (++nextGeneration==0) ++nextGeneration;
            slot.generation=nextGeneration;
            slot.first=first; slot.allocation=rounded; slot.reserved=true;
            mesh.slot=slotIndex; mesh.generation=slot.generation;
            mesh.firstVertex=first; mesh.vertexCount=count;
            for (unsigned i=0;i<3;++i)
            {
                mesh.origin[i]=static_cast<float>(request.origin[i])*config.baseSpacing;
                mesh.baseChunkCoordinate[i]=request.origin[i]/static_cast<std::int32_t>(config.cells);
            }
            mesh.extent=static_cast<float>(config.cells)*(static_cast<float>(1u<<request.lod)*config.baseSpacing);
            mesh.lod=request.lod; mesh.transitionMask=request.transitionMask;
            slot.mesh=mesh;
            statistics.allocatedBytes+=static_cast<std::uint64_t>(rounded)*VertexBytes;
            Descriptor descriptor{{mesh.origin[0],mesh.origin[1],mesh.origin[2],mesh.extent},
                {request.lod,static_cast<std::uint32_t>(mesh.baseChunkCoordinate[0]),
                 static_cast<std::uint32_t>(mesh.baseChunkCoordinate[1]),
                 static_cast<std::uint32_t>(mesh.baseChunkCoordinate[2])}};
            glNamedBufferSubData(descriptors,static_cast<GLintptr>(slotIndex)*sizeof(Descriptor),sizeof(Descriptor),&descriptor);
            return true;
        }

        void freeSlot(std::uint32_t index)
        {
            auto& slot=slots[index];
            if (!slot.reserved) return;
            auto first=slot.first, count=slot.allocation;
            statistics.allocatedBytes-=static_cast<std::uint64_t>(count)*VertexBytes;
            auto next=freeRanges.lower_bound(first);
            if (next!=freeRanges.begin())
            {
                auto previous=std::prev(next);
                if (previous->first+previous->second==first)
                { first=previous->first; count+=previous->second; freeRanges.erase(previous); }
            }
            next=freeRanges.lower_bound(first);
            if (next!=freeRanges.end() && first+count==next->first)
            { count+=next->second; freeRanges.erase(next); }
            freeRanges.emplace(first,count);
            slot.reserved=false; slot.live=false; slot.allocation=0;
            freeSlots.push_back(index);
        }

        void bindJob(const Job& job)
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,job.density);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,job.columns);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,tables);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,job.counter);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,biomeCells);
        }

        void regular(const Program& program, const Job& job)
        {
            program.request(job.request,job.mesh.vertexCount);
            glDispatchCompute((config.cells+7)/8,(config.cells+3)/4,(config.cells+3)/4);
        }

        void transition(const Program& program, const Job& job)
        {
            if (!job.request.transitionMask) return;
            program.request(job.request,job.mesh.vertexCount);
            const auto faceCells=(config.cells/2)*(config.cells/2);
            glDispatchCompute((faceCells+63)/64,6,1);
        }

        void count(Job& job)
        {
            bindJob(job);
            const auto& columns=programs[Columns];
            const auto& density=programs[Density];
            const auto side=config.cells+3;
            glBeginQuery(GL_TIME_ELAPSED,job.timers[0]);
            columns.request(job.request);
            glDispatchCompute((side+7)/8,(side+7)/8,1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            density.request(job.request);
            glDispatchCompute((side+7)/8,(side+3)/4,(side+3)/4);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            regular(programs[CountRegular],job);
            if (job.request.transitionMask)
            {
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                transition(programs[CountTransition],job);
            }
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
            glEndQuery(GL_TIME_ELAPSED);
            job.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            flushPending=true;
            job.stage=JobStage::Counting;
            if (!job.fence) { broken=true; error="Cannot create terrain generation fence"; }
        }

        void emit(Job& job)
        {
            bindJob(job);
            const auto& slot=slots[job.mesh.slot];
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER,4,vertices,
                static_cast<GLintptr>(slot.first)*VertexBytes,
                static_cast<GLsizeiptr>(slot.allocation)*VertexBytes);
            glBeginQuery(GL_TIME_ELAPSED,job.timers[1]);
            regular(programs[EmitRegular],job);
            if (job.request.transitionMask)
            {
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                transition(programs[EmitTransition],job);
            }
            // poll() validates cursor/overflow before publishing a handle.
            // Draw lists use that validated handle; no GPU command-copy pass.
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT|
                GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
            glEndQuery(GL_TIME_ELAPSED);
            job.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            flushPending=true;
            job.stage=JobStage::Emitting;
            if (!job.fence) { broken=true; error="Cannot create terrain emission fence"; }
        }
    };

    GpuMesher::GpuMesher() : m_impl(new Impl) {}
    GpuMesher::~GpuMesher() { shutdown(); }

    bool GpuMesher::initialize(const GpuConfig& config, std::string& error)
    {
        shutdown();
        auto& s=*m_impl;
        s.config=config; s.error.clear();
        if (config.cells<4 || config.cells>64 || (config.cells&(config.cells-1))!=0 ||
            !finiteFieldValue(config.baseSpacing) || config.baseSpacing<=0.0f ||
            config.baseSpacing<(std::numeric_limits<float>::min)()*65534.0f/config.cells ||
            config.vertexBudgetMiB==0 || config.vertexBudgetMiB>4096 ||
            config.maxResidentChunks==0 || config.maxResidentChunks>65536 ||
            config.maxInFlight==0 || config.maxInFlight>16 ||
            !validFieldParameters(config.field))
        { error=s.error="Invalid procedural terrain GPU configuration"; return false; }
        const auto driverString = [](GLenum name)
        {
            const auto* value = glGetString(name);
            return value ? reinterpret_cast<const char*>(value) : "unavailable";
        };
        std::cerr << "[Terrain initialization] OpenGL " << driverString(GL_VERSION)
            << "; vendor: " << driverString(GL_VENDOR)
            << "; renderer: " << driverString(GL_RENDERER) << std::endl;
        GLint bindings=0, alignment=0;
        GLint64 maximumBlock=0;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&bindings);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&alignment);
        glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&maximumBlock);
        if (bindings<9 || alignment<=0 || maximumBlock<=0)
        { error=s.error="Terrain requires nine SSBO binding points and persistent buffer mapping"; return false; }
        s.maximumStorageBlock=static_cast<std::uint64_t>(maximumBlock);
        s.vertexAlignment=static_cast<std::uint32_t>(alignment)/std::gcd(static_cast<std::uint32_t>(alignment),VertexBytes);
        const std::uint64_t budget=static_cast<std::uint64_t>(config.vertexBudgetMiB)*1024*1024;
        const auto vertexCapacity=static_cast<std::uint32_t>(budget/VertexBytes/s.vertexAlignment*s.vertexAlignment);
        if (vertexCapacity==0) { error=s.error="Terrain vertex budget is too small"; return false; }
        s.statistics.budgetBytes=static_cast<std::uint64_t>(vertexCapacity)*VertexBytes;

        const char* files[ProgramCount]={"field_columns.C.glsl","field_density.C.glsl","meshing.C.glsl",
            "transition.C.glsl","meshing.C.glsl","transition.C.glsl"};
        Ge::ShaderCacheOptions shaderCache;
        shaderCache.enabled=true;
        for (unsigned i=0;i<ProgramCount;++i)
        {
            const bool emitPass = i==EmitRegular || i==EmitTransition;
            const char* define=emitPass?"#define EMIT_PASS 1\n":"#define EMIT_PASS 0\n";
            const std::string label="Terrain "+std::to_string(i+1)+"/"+std::to_string(ProgramCount)
                +" "+files[i]+" EMIT_PASS="+(emitPass ? "1" : "0");
            Ge::ComputeShader shader(config.shaderDirectory+"/"+files[i],define,label,shaderCache);
            if (!shader.isValid())
            { error=shader.getLastError(); shutdown(); s.error=error; return false; }
            auto& program=s.programs[i];
            program.id=shader.takeProgram();
            program.discover();
            program.field(config);
        }
        glCreateBuffers(1,&s.biomeCells);
        constexpr auto biomeCells=makeBiomeCellTable();
        glNamedBufferStorage(s.biomeCells,sizeof(biomeCells),biomeCells.data(),0);
        s.statistics.biomeMetadataBytes=sizeof(biomeCells)+config.maxInFlight*sizeof(Counters);

        glCreateBuffers(1,&s.vertices);
        glNamedBufferData(s.vertices,static_cast<GLsizeiptr>(s.statistics.budgetBytes),nullptr,GL_DYNAMIC_DRAW);
        glCreateBuffers(1,&s.tables);
        const auto table=Transvoxel::makePackedTables();
        glNamedBufferData(s.tables,sizeof(table),table.data(),GL_STATIC_DRAW);
        glCreateBuffers(1,&s.descriptors);
        glNamedBufferData(s.descriptors,static_cast<GLsizeiptr>(config.maxResidentChunks)*sizeof(Descriptor),nullptr,GL_DYNAMIC_DRAW);
        glCreateVertexArrays(1,&s.vao);
        glVertexArrayVertexBuffer(s.vao,0,s.vertices,0,VertexBytes);
        for (unsigned i=0;i<3;++i)
        {
            glEnableVertexArrayAttrib(s.vao,i);
            glVertexArrayAttribIFormat(s.vao,i,1,GL_UNSIGNED_INT,i*4);
            glVertexArrayAttribBinding(s.vao,i,0);
        }
        s.slots.resize(config.maxResidentChunks);
        s.freeSlots.reserve(config.maxResidentChunks);
        s.retired.reserve(config.maxResidentChunks);
        s.drawScratch.reserve(config.maxResidentChunks);
        for (std::uint32_t i=config.maxResidentChunks;i>0;--i) s.freeSlots.push_back(i-1);
        s.freeRanges.emplace(0,vertexCapacity);

        const std::uint64_t side=config.cells+3;
        s.statistics.transientFieldBytes=config.maxInFlight*
            (side*side*side*sizeof(float)+side*side*FieldColumnBytes);
        s.jobs.resize(config.maxInFlight);
        for (auto& job:s.jobs)
        {
            glCreateBuffers(1,&job.density);
            glNamedBufferData(job.density,static_cast<GLsizeiptr>(side*side*side*sizeof(float)),nullptr,GL_DYNAMIC_DRAW);
            glCreateBuffers(1,&job.columns);
            glNamedBufferData(job.columns,static_cast<GLsizeiptr>(side*side*FieldColumnBytes),nullptr,GL_DYNAMIC_DRAW);
            glCreateBuffers(1,&job.counter);
            const Counters zero{};
            glNamedBufferStorage(job.counter,sizeof(Counters),&zero,
                GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT|GL_DYNAMIC_STORAGE_BIT);
            job.mapped=static_cast<const volatile Counters*>(glMapNamedBufferRange(job.counter,0,sizeof(Counters),
                GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT));
            glGenQueries(2,job.timers);
            if (!job.mapped)
            { error="Cannot map terrain counter buffer"; shutdown(); s.error=error; return false; }
        }
        const GLenum glError=glGetError();
        if (glError!=GL_NO_ERROR)
        { error="OpenGL terrain allocation failed (error "+std::to_string(glError)+")"; shutdown(); s.error=error; return false; }
        s.initialized=true;
        return true;
    }

    void GpuMesher::shutdown() noexcept
    {
        auto& s=*m_impl;
        for (auto& job:s.jobs)
        {
            if (job.fence) glDeleteSync(job.fence);
            if (job.mapped) glUnmapNamedBuffer(job.counter);
            if (job.timers[0]) glDeleteQueries(2,job.timers);
            const GLuint buffers[]={job.density,job.columns,job.counter};
            glDeleteBuffers(3,buffers);
        }
        for (auto& item:s.retired) if (item.fence) glDeleteSync(item.fence);
        for (auto& program:s.programs) { if (program.id) glDeleteProgram(program.id); program=Program{}; }
        if (s.vao) glDeleteVertexArrays(1,&s.vao);
        for (auto& entry:s.drawLists)
            if (entry.second.buffer) glDeleteBuffers(1,&entry.second.buffer);
        s.drawLists.clear();
        const GLuint buffers[]={s.vertices,s.tables,s.descriptors,s.biomeCells};
        if (s.vertices || s.tables || s.descriptors || s.biomeCells) glDeleteBuffers(4,buffers);
        s.vertices=s.vao=s.tables=s.descriptors=s.biomeCells=0;
        s.jobs.clear(); s.slots.clear(); s.freeSlots.clear(); s.retired.clear(); s.freeRanges.clear();
        s.drawScratch.clear();
        s.initialized=false; s.broken=false; s.flushPending=false; s.statistics=GpuStats{};
    }

    bool GpuMesher::submit(const ChunkRequest& request, std::uint64_t token)
    {
        auto& s=*m_impl;
        if (!s.initialized || s.broken) return false;
        s.error.clear();
        if (request.lod>20 || (request.transitionMask&~63u)!=0)
        { s.error="Invalid terrain chunk LOD or transition mask"; return false; }
        const std::int64_t step=std::int64_t{1}<<request.lod;
        const std::int64_t span=step*s.config.cells;
        double maximumWorldCoordinate=0.0;
        for (auto origin:request.origin)
        {
            if (origin%span!=0 || static_cast<std::int64_t>(origin)-step<INT32_MIN ||
                static_cast<std::int64_t>(origin)+(s.config.cells+1)*step>INT32_MAX)
            { s.error="Terrain chunk is unaligned or outside the integer lattice"; return false; }
            const std::int64_t baseChunk=static_cast<std::int64_t>(origin)/s.config.cells;
            if (baseChunk<-(std::int64_t{1}<<24) || baseChunk>(std::int64_t{1}<<24))
            { s.error="Terrain chunk exceeds the exact floating-point chunk coordinate domain"; return false; }
            const double lower=static_cast<double>(static_cast<std::int64_t>(origin)-step)*s.config.baseSpacing;
            const double upper=static_cast<double>(static_cast<std::int64_t>(origin)+(s.config.cells+1)*step)*s.config.baseSpacing;
            maximumWorldCoordinate=(std::max)(maximumWorldCoordinate,(std::max)(std::abs(lower),std::abs(upper)));
        }
        if (!validFieldDomain(s.config.field,maximumWorldCoordinate))
        { s.error="Terrain chunk exceeds the finite procedural field domain"; return false; }
        for (const auto& job:s.jobs)
            if (job.stage!=JobStage::Free && job.token==token)
            { s.error="Duplicate terrain job token"; return false; }
        auto found=std::find_if(s.jobs.begin(),s.jobs.end(),[](const auto& job){return job.stage==JobStage::Free;});
        if (found==s.jobs.end()) return false;
        auto& job=*found;
        job.request=request; job.token=token; job.cancelled=false; job.mesh=MeshHandle{};
        const Counters zero{};
        glNamedBufferSubData(job.counter,0,sizeof(Counters),&zero);
        // Climate, local biome weights and relief stay on the GPU. The
        // columns/density/count dispatches share one submission, with no
        // classification readback or runtime program selection in between.
        ++s.statistics.pendingJobs;
        s.count(job);
        if (s.broken) return false;
        return true;
    }

    void GpuMesher::poll(std::vector<Completion>& completions)
    {
        auto& s=*m_impl;
        if (!s.initialized) return;
        const auto failPending=[&]()
        {
            // A failed fence cannot authorize buffer reuse. Keep allocations
            // and GL objects until shutdown; report every pending token once.
            for (auto& pending:s.jobs)
            {
                if (pending.stage==JobStage::Free) continue;
                Completion failure;
                failure.token=pending.token;
                failure.error=s.error;
                completions.push_back(std::move(failure));
                pending.stage=JobStage::Free;
                ++s.statistics.failedJobs;
            }
            s.statistics.pendingJobs=0;
        };
        if (s.broken) { failPending(); return; }
        // One submission for all newly queued generation/retirement fences.
        // In particular, retirement must progress even when no jobs remain.
        if (s.flushPending) { glFlush(); s.flushPending=false; }
        for (auto it=s.retired.begin();it!=s.retired.end();)
        {
            const auto status=glClientWaitSync(it->fence,0,0);
            if (status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED)
            { glDeleteSync(it->fence); s.freeSlot(it->slot); it=s.retired.erase(it); }
            else if (status==GL_WAIT_FAILED)
            { s.error="Terrain retirement fence failed"; s.broken=true; failPending(); return; }
            else ++it;
        }
        for (auto& job:s.jobs)
        {
            if (job.stage==JobStage::Free) continue;
            const auto status=glClientWaitSync(job.fence,0,0);
            if (status==GL_TIMEOUT_EXPIRED) continue;
            if (status==GL_WAIT_FAILED)
            { s.error="Terrain GPU fence failed"; s.broken=true; failPending(); return; }
            glDeleteSync(job.fence); job.fence=nullptr;
            Completion completion;
            completion.token=job.token;
            if (job.cancelled)
            {
                if (job.mesh.slot!=UINT32_MAX) s.freeSlot(job.mesh.slot);
                completion.status=CompletionStatus::Cancelled;
            }
            else if (job.stage==JobStage::Counting)
            {
                const std::uint32_t count=job.mapped->total;
                if (job.mapped->overflow!=0)
                { completion.error="Terrain count overflow"; ++s.statistics.failedJobs; }
                else if (count==0)
                { completion.status=CompletionStatus::Empty; ++s.statistics.emptyJobs; }
                else if (!s.allocate(count,job.mesh,job.request))
                { completion.error=s.error; ++s.statistics.failedJobs; }
                else
                {
                    s.emit(job);
                    if (s.broken) { failPending(); return; }
                    continue;
                }
            }
            else
            {
                if (job.mapped->overflow!=0 || job.mapped->cursor!=job.mesh.vertexCount)
                {
                    completion.error="Terrain mesh emission exceeded its reserved allocation";
                    s.freeSlot(job.mesh.slot); ++s.statistics.failedJobs;
                }
                else
                {
                    completion.status=CompletionStatus::Ready; completion.mesh=job.mesh;
                    s.slots[job.mesh.slot].live=true;
                    ++s.statistics.residentChunks; s.statistics.residentVertices+=job.mesh.vertexCount;
                    ++s.statistics.completedJobs;
                    // Timing is diagnostic only. Never make mesh publication
                    // wait for a driver's delayed query-result delivery.
                    GLint available0=GL_FALSE,available1=GL_FALSE;
                    glGetQueryObjectiv(job.timers[0],GL_QUERY_RESULT_AVAILABLE,&available0);
                    glGetQueryObjectiv(job.timers[1],GL_QUERY_RESULT_AVAILABLE,&available1);
                    if (available0 && available1)
                    {
                        GLuint64 time0=0,time1=0;
                        glGetQueryObjectui64v(job.timers[0],GL_QUERY_RESULT,&time0);
                        glGetQueryObjectui64v(job.timers[1],GL_QUERY_RESULT,&time1);
                        s.statistics.lastGenerationMilliseconds=static_cast<double>(time0+time1)/1000000.0;
                    }
                }
            }
            if (!completion.error.empty()) s.error=completion.error;
            completions.push_back(std::move(completion));
            job.stage=JobStage::Free;
            --s.statistics.pendingJobs;
        }
    }

    void GpuMesher::cancel(std::uint64_t token) noexcept
    {
        for (auto& job:m_impl->jobs)
            if (job.stage!=JobStage::Free && job.token==token) job.cancelled=true;
    }

    bool GpuMesher::valid(const MeshHandle& mesh) const noexcept
    {
        const auto& s=*m_impl;
        return s.initialized && !s.broken && mesh.slot<s.slots.size() && s.slots[mesh.slot].live &&
            s.slots[mesh.slot].generation==mesh.generation;
    }

    void GpuMesher::release(const MeshHandle& mesh) noexcept
    {
        if (!valid(mesh)) return;
        auto& s=*m_impl;
        s.slots[mesh.slot].live=false;
        --s.statistics.residentChunks; s.statistics.residentVertices-=mesh.vertexCount;
        const GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if (!fence) { s.broken=true; s.error="Cannot create terrain retirement fence"; return; }
        s.flushPending=true;
        s.retired.push_back({mesh.slot,fence});
    }

    void GpuMesher::draw(const MeshHandle& mesh) const
    {
        if (!valid(mesh)) return;
        const auto& s=*m_impl;
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,s.descriptors);
        glBindVertexArray(s.vao);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES,static_cast<GLint>(mesh.firstVertex),
            static_cast<GLsizei>(mesh.vertexCount),1,mesh.slot);
    }

    void GpuMesher::drawMany(const std::vector<MeshHandle>& meshes, std::uint32_t drawList)
    {
        auto& s=*m_impl;
        if (!s.initialized) return;
        auto& commands=s.drawScratch;
        commands.clear();
        for (const auto& mesh:meshes)
            if (valid(mesh)) commands.push_back({mesh.vertexCount,1,mesh.firstVertex,mesh.slot});
        if (commands.empty()) return;
        auto& cache=s.drawLists[drawList];
        const bool changed=commands.size()!=cache.commands.size() ||
            std::memcmp(commands.data(),cache.commands.data(),commands.size()*sizeof(Command))!=0;
        if (changed)
        {
            // Only 16-byte draw descriptors cross the bus; geometry remains in
            // its GPU allocation. Orphaning avoids overwriting in-flight draws.
            if (!cache.buffer) glCreateBuffers(1,&cache.buffer);
            const auto bytes=commands.size()*sizeof(Command);
            glNamedBufferData(cache.buffer,static_cast<GLsizeiptr>(bytes),commands.data(),GL_STREAM_DRAW);
            cache.commands.swap(commands);
            ++s.statistics.drawListUploads;
            s.statistics.drawListUploadBytes+=bytes;
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,s.descriptors);
        glBindVertexArray(s.vao);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER,cache.buffer);
        glMultiDrawArraysIndirect(GL_TRIANGLES,nullptr,static_cast<GLsizei>(cache.commands.size()),sizeof(Command));
    }

    unsigned int GpuMesher::vertexBuffer() const noexcept { return m_impl->vertices; }
    unsigned int GpuMesher::descriptorBuffer() const noexcept { return m_impl->descriptors; }
    unsigned int GpuMesher::vertexArray() const noexcept { return m_impl->vao; }
    const GpuConfig& GpuMesher::config() const noexcept { return m_impl->config; }
    GpuStats GpuMesher::stats() const noexcept { return m_impl->statistics; }
    const std::string& GpuMesher::lastError() const noexcept { return m_impl->error; }
}
