// Real GL persistent staging and real Bullet geometry, with only the engine's
// mesh-residency lookup and physics publication queue replaced by test adapters.
#include "glcore.hpp"
#include "ProceduralTerrainCollision.hpp"
#include "PhysicsWraper.hpp"
#include "StaticMeshCollider.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace ProceduralTerrain;
    GLuint source=0;
    std::vector<MeshHandle> current;
    std::thread::id ownerThread;
    unsigned readbackCalls=0;
    bool workerGlCall=false;
    std::vector<GLuint> mappedBuffers;
    PFNGLGETNAMEDBUFFERSUBDATAPROC originalReadback=nullptr;
    PFNGLMAPNAMEDBUFFERRANGEPROC originalMap=nullptr;
    void require(bool value,const char* error) { if(!value) throw std::runtime_error(error); }
    void GLAPIENTRY readback(GLuint buffer,GLintptr offset,GLsizeiptr length,void* data)
    { ++readbackCalls; originalReadback(buffer,offset,length,data); }
    void* GLAPIENTRY map(GLuint buffer,GLintptr offset,GLsizeiptr length,GLbitfield flags)
    {
        workerGlCall|=std::this_thread::get_id()!=ownerThread;
        mappedBuffers.push_back(buffer);
        return originalMap(buffer,offset,length,flags);
    }
    struct Observe
    {
        Observe()
        {
            ownerThread=std::this_thread::get_id();
            originalReadback=__glewGetNamedBufferSubData; __glewGetNamedBufferSubData=readback;
            originalMap=__glewMapNamedBufferRange; __glewMapNamedBufferRange=map;
        }
        ~Observe()
        { __glewGetNamedBufferSubData=originalReadback; __glewMapNamedBufferRange=originalMap; }
    };
    std::array<std::uint32_t,3> vertex(unsigned x,unsigned y,unsigned z)
    { return {x|(y<<16u),z,0u}; }
    void makeMeshes(unsigned count,unsigned generation=1,unsigned repeats=1)
    {
        std::vector<std::array<std::uint32_t,3>> packed;
        current.clear();
        for(unsigned i=0;i<count;++i)
        {
            MeshHandle mesh;
            mesh.slot=i; mesh.generation=generation; mesh.extent=16.0f;
            mesh.baseChunkCoordinate={int(i)-2,0,0};
            mesh.origin={float(int(i)-2)*16,0,0};
            mesh.firstVertex=static_cast<unsigned>(packed.size());
            for(unsigned n=0;n<repeats;++n)
            {
                packed.push_back(vertex(317,278,111));
                packed.push_back(vertex(35000,278,111));
                packed.push_back(vertex(317,278,40000));
            }
            // Degenerate triangles must not become physics surfaces.
            for(unsigned n=0;n<3;++n) packed.push_back(vertex(317,278,111));
            mesh.vertexCount=static_cast<unsigned>(packed.size())-mesh.firstVertex;
            current.push_back(mesh);
        }
        if(!source) glCreateBuffers(1,&source);
        glNamedBufferData(source,static_cast<GLsizeiptr>(packed.size()*12),packed.data(),GL_STATIC_DRAW);
    }
    template<class Predicate> void pump(CollisionCache& cache,const GpuMesher& mesher,
                                         const glm::vec3& camera,Predicate done)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        do
        {
            cache.update(camera,current,mesher);
            const auto stats=cache.stats();
            require(stats.pendingBuilds<=4 && stats.pendingReadbacks<=2,"Collision queues exceeded their bounds");
            if(done()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("Collision fixture did not make progress");
    }
    void check()
    {
        Observe observe;
        Ge::PhysicsWraper physics;
        GpuMesher mesher;
        CollisionCache cache;
        struct Unblock { Ge::PhysicsWraper& value; ~Unblock(){value.block(false);} } unblock{physics};
        CollisionConfig config;
        config.radius=512; config.maxResidentChunks=32;
        config.maxCopiesPerFrame=2;
        auto invalid=config; invalid.workerCount=0;
        require(!cache.initialize(&physics,invalid),"Invalid worker count accepted");
        require(cache.initialize(&physics,config),"Collision initialization failed");
        makeMeshes(6);
        physics.block(true);
        pump(cache,mesher,glm::vec3(0),[&]
        {
            std::lock_guard<std::mutex> lock(physics.mutex);
            return physics.entered==2 && cache.stats().pendingBuilds==4;
        });
        require(cache.stats().copiedBytes>=4*6*12,"Staging slots stayed pinned through physics publication");
        require(cache.stats().stagingBytes==2*6*12,"Persistent staging budget is incorrect");
        physics.block(false);
        pump(cache,mesher,glm::vec3(0),[&]
            { return cache.stats().residentChunks==6 && !cache.stats().pendingBuilds && !cache.stats().pendingReadbacks; });
        {
            std::lock_guard<std::mutex> lock(physics.mutex);
            require(!physics.invalid && physics.live.size()==6,"Bullet mesh/publication ownership was invalid");
            require(physics.builders.size()<=2 && !physics.builders.count(ownerThread),
                    "Collision work escaped the fixed pool or ran on the GL thread");
            require(physics.triangles.size()==6,"Degenerate triangles were retained");
            const float scale=16.0f/65534.0f;
            std::set<float> expectedX, actualX;
            for(unsigned i=0;i<6;++i) expectedX.insert(std::fma(float(int(i)-2),65534.0f,317.0f)*scale);
            for(const auto& triangle:physics.triangles)
            {
                actualX.insert(triangle[0]);
                require(triangle[1]==278.0f*scale && triangle[2]==111.0f*scale,
                        "Persistent decode changed canonical world positions");
            }
            require(actualX==expectedX,"Negative/positive chunk FMA decode changed");
        }
        // A replacement grows immutable staging safely after the old decode.
        makeMeshes(6,2,3);
        pump(cache,mesher,glm::vec3(0),[&]
            { return cache.stats().completedChunks==12 && !cache.stats().pendingBuilds && !cache.stats().pendingReadbacks; });
        require(cache.stats().stagingBytes==2*12*12,"Growing staging did not replace its persistent mappings");
        {
            std::lock_guard<std::mutex> lock(physics.mutex);
            require(physics.live.size()==6 && physics.releases==6,"Collider replacement leaked or removed coverage early");
        }
        require(!workerGlCall && readbackCalls==0,"Collision pipeline used a GL readback or worker GL call");
        cache.shutdown();
        for(auto id:mappedBuffers) require(!glIsBuffer(id),"Shutdown leaked a mapped staging buffer");
        {
            std::lock_guard<std::mutex> lock(physics.mutex);
            require(physics.live.empty(),"Shutdown leaked a collider");
        }

        // Cancel both decoding/queued and already-constructed bodies while a
        // physics operation is pending. No late result may publish out of range.
        mappedBuffers.clear();
        require(cache.initialize(&physics,config),"Collision reinitialization failed");
        makeMeshes(6,3);
        physics.block(true);
        unsigned entered=0;
        { std::lock_guard<std::mutex> lock(physics.mutex); entered=physics.entered; }
        pump(cache,mesher,glm::vec3(0),[&]
        {
            std::lock_guard<std::mutex> lock(physics.mutex);
            return physics.entered>=entered+2 && cache.stats().pendingBuilds==4;
        });
        cache.update(glm::vec3(10000),current,mesher);
        physics.block(false);
        pump(cache,mesher,glm::vec3(10000),[&]
            { return !cache.stats().pendingBuilds && !cache.stats().pendingReadbacks; });
        require(cache.stats().residentChunks==0 && cache.stats().failedChunks==0,
                "Cancelled collision work published or became a false failure");
        cache.shutdown();
        { std::lock_guard<std::mutex> lock(physics.mutex); require(physics.live.empty(),"Cancellation leaked collider ownership"); }
        glDeleteBuffers(1,&source); source=0;
        require(glGetError()==GL_NO_ERROR,"Collision fixture produced an OpenGL error");
        std::cout<<"PASS collision: real persistent transfers, bounded workers/builds, early staging reuse, canonical Bullet geometry, replacement, cancellation and shutdown\n";
    }
}

namespace ProceduralTerrain
{
    struct GpuMesher::Impl {};
    GpuMesher::GpuMesher():m_impl(std::make_unique<Impl>()) {}
    GpuMesher::~GpuMesher()=default;
    unsigned int GpuMesher::vertexBuffer() const noexcept {return source;}
    bool GpuMesher::valid(const MeshHandle& mesh) const noexcept
    { return std::any_of(current.begin(),current.end(),[&](const MeshHandle& item)
        {return item.slot==mesh.slot && item.generation==mesh.generation;}); }
}

void Ge::PhysicsWraper::block(bool value)
{ {std::lock_guard<std::mutex> lock(mutex); blocked=value;} wake.notify_all(); }
Ge::StaticMeshCollider* Ge::PhysicsWraper::AllocateStaticMeshCollider(btBvhTriangleMeshShape* shape,
    glm::vec3 position,glm::quat rotation,glm::vec3 scale,std::shared_ptr<void> owner)
{
    const unsigned char* vertices=nullptr;
    const unsigned char* indices=nullptr;
    int vertexCount=0,vertexStride=0,indexStride=0,faceCount=0;
    PHY_ScalarType vertexType,indexType;
    shape->getMeshInterface()->getLockedReadOnlyVertexIndexBase(&vertices,vertexCount,vertexType,
        vertexStride,&indices,indexStride,faceCount,indexType);
    std::array<float,9> points{};
    bool valid=position==glm::vec3(0) && rotation==glm::quat(1,0,0,0) && scale==glm::vec3(1)
        && vertexType==PHY_FLOAT && indexType==PHY_INTEGER && (faceCount==1 || faceCount==3);
    if(valid) for(unsigned corner=0;corner<3;++corner)
    {
        int index=0; std::memcpy(&index,indices+corner*sizeof(int),sizeof(index));
        if(index<0 || index>=vertexCount) {valid=false; break;}
        std::memcpy(points.data()+3*corner,vertices+index*vertexStride,3*sizeof(float));
    }
    shape->getMeshInterface()->unLockReadOnlyVertexBase(0);
    auto* result=new StaticMeshCollider{shape,std::move(owner)};
    std::unique_lock<std::mutex> lock(mutex);
    invalid|=!valid; builders.insert(std::this_thread::get_id()); ++entered;
    triangles.push_back(points);
    wake.wait(lock,[this]{return !blocked;});
    live.insert(result);
    return result;
}
void Ge::PhysicsWraper::ReleaseStaticMeshCollider(StaticMeshCollider* collider)
{
    {std::lock_guard<std::mutex> lock(mutex); invalid|=live.erase(collider)!=1; ++releases;}
    delete collider;
}

extern "C" int terrain_collision_tests()
{
    try
    {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        check();
        return 0;
    }
    catch(const std::exception& error)
    {std::cerr<<"FAIL collision: "<<error.what()<<'\n';return 1;}
}
