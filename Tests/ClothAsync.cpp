// Runs the production worker, solver and deformation with a small imported
// mesh/rig facade and headless GL. No delay hook is injected into production.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include "SkinnedCloth.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "SkinnedTransform.hpp"
#include "glcore.hpp"

// Fail one large buffer growth, allowing CRT/STL bookkeeping and exception
// cleanup to allocate. In MSVC Debug a noexcept vector constructor allocates
// a small iterator proxy; denying that allocation terminates before the worker
// can catch, which would test the CRT rather than our exception boundary.
static std::atomic<bool> failWorkerAllocation{false};
void* operator new(std::size_t size) {
    if (size>=1024 && std::this_thread::get_id()!=ClothTest::mainThread &&
        failWorkerAllocation.exchange(false,std::memory_order_relaxed)) throw std::bad_alloc();
    if (void* result=std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr,std::size_t) noexcept { std::free(ptr); }

namespace {
using Clock=std::chrono::steady_clock;
void check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"CHECK FAILED before cleanup: "<<message<<std::endl;throw std::runtime_error(message);}
}
bool finite(const glm::vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
std::vector<unsigned char> bytes(const Ge::SkinnedCloth& cloth) { return ClothTest::buffers.at(cloth.getBufferID(0)); }
void geometry(const Ge::SkinnedCloth& cloth) {
    check(!cloth.renderVertices().empty(),"empty completed geometry");
    for(const auto& vertex:cloth.renderVertices()) {
        check(finite(glm::vec3(vertex.position))&&finite(glm::vec3(vertex.normal))&&finite(glm::vec3(vertex.tangent)),"torn/nonfinite output snapshot");
        check(std::abs(glm::length(glm::vec3(vertex.normal))-1)<.002f,"invalid completed normal");
    }
}
bool same(const glm::mat4& a,const glm::mat4& b) {
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)if(std::abs(a[i][j]-b[i][j])>1e-5f)return false;
    return true;
}
template<class Predicate>
void eventually(Ge::SkinnedCloth& cloth, const std::vector<ClothCapsule>& caps,
                Predicate predicate,const char* message) {
    const auto deadline=Clock::now()+std::chrono::seconds(15);
    do {
        check(cloth.update(1.f/120,caps),"worker failed during update");
        if(predicate())return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }while(Clock::now()<deadline);
    check(predicate(),message);
}
struct Fixture {
    std::vector<float> pins;
    Ge::ShapeBufferBase shape;
    Ge::ShapeBufferSkeleton buffer;
    Ge::SkinnedTransform first,second;
    std::shared_ptr<const Ge::SkinnedClothMesh> mesh;
    Fixture():shape(vertices(),indices()),buffer(&shape),first(&buffer),second(&buffer) {
        constexpr unsigned side=17;
        pins.resize(side*side,0);
        for(unsigned x=0;x<side;++x) { pins[x]=1; pins[side+x]=.6f; }
        ClothBuildSettings build;build.maxParticles=350;
        mesh=Ge::SkinnedClothMesh::build(&buffer,pins,build);
        check(bool(mesh),"synthetic immutable topology failed");
    }
    static std::vector<Vertex> vertices() {
        constexpr unsigned side=17;std::vector<Vertex> out;
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
            Vertex v{};v.pos={float(x)/16-0.5f,1.8f-float(y)*1.8f/16,0};
            v.normal={0,0,1};v.tangents={1,0,0};v.texCoord={float(x)/16,float(y)/16};
            const std::uint32_t packed[3]={0,65535,0};std::memcpy(&v.color,packed,sizeof(packed));out.push_back(v);
        }return out;
    }
    static std::vector<std::uint32_t> indices() {
        constexpr unsigned side=17;std::vector<std::uint32_t> out;
        for(unsigned y=0;y<side-1;++y)for(unsigned x=0;x<side-1;++x) {
            const auto a=y*side+x,b=a+1,c=a+side,d=c+1;
            out.insert(out.end(),{a,c,b,b,c,d});
        }return out;
    }
};
}

int main() {
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    std::cout<<"PHASE fixture construction"<<std::endl;
    try {
        Fixture fixture;std::cout<<"PHASE fixture ready; bind two cloth instances"<<std::endl;Ge::SkinnedCloth first,second;ClothSettings settings;
        settings.iterations=16;settings.maxSubsteps=1;
        check(first.bind(fixture.mesh,&fixture.first,settings)&&second.bind(fixture.mesh,&fixture.second,settings),"bind failed");
        check(first.getBufferID(0)!=second.getBufferID(0),"instances share SSBO");geometry(first);
        std::cout<<"PHASE bound; start two workers"<<std::endl;
        check(first.startAsync()&&second.startAsync(),"persistent workers failed to start");
        check(first.asyncEnabled()&&first.simulation()==nullptr,"running worker exposes live mutable solver");
        check(first.startAsync(),"starting existing worker should be idempotent");
        std::vector<ClothCapsule> heavyCaps(24,{{0,.9f,-.17f},{0,1.4f,-.17f},.12f,.3f});
        std::cout<<"PHASE workers started; wait first result"<<std::endl;
        eventually(first,heavyCaps,[&]{return first.asyncDiagnostics().workerSeconds>0;},"first asynchronous result missing");
        std::cout<<"PHASE first result ready; wait second result"<<std::endl;
        eventually(second,{},[&]{return second.asyncDiagnostics().workerSeconds>0;},"second asynchronous result missing");
        geometry(first);geometry(second);
        std::cout<<"PASS persistent production worker/shared immutable mesh/independent SSBO/main-owned geometry\n";

        std::cout<<"PHASE overload producer"<<std::endl;
        const double beginning=first.asyncDiagnostics().gameSeconds;
        auto prior=bytes(first);unsigned retained=0;const auto initialEpoch=first.currentEpoch();
        for(unsigned i=0;i<250;++i) {
            fixture.first.pose[0]=glm::translate(glm::mat4(1),glm::vec3(float(i)*.00001f,0,0));
            check(first.update(.001f,heavyCaps),"input publication failed");
            auto current=bytes(first);retained+=current==prior;prior=std::move(current);
        }
        eventually(first,heavyCaps,[&]{return first.asyncDiagnostics().gameSeconds>=beginning+.245;},"coalescing lost cumulative game time");
        check(first.currentEpoch()==initialEpoch,"ordinary pose changes reset worker");
        check(retained>125,"render submitted work synchronously instead of retaining completed snapshot");
        check(first.asyncDiagnostics().epoch==first.currentEpoch(),"consumed stale generation");
        check(first.asyncDiagnostics().substeps<=1,"worker exceeded bounded catch-up budget");geometry(first);
        std::cout<<"PASS overloaded render submissions keep coherent old result/coalesced clock/bounded substeps\n";

        std::cout<<"PHASE pause freeze"<<std::endl;
        check(first.update(0,heavyCaps),"pause failed");const auto paused=bytes(first);const auto uploadCount=ClothTest::uploads;
        for(unsigned i=0;i<20;++i) {std::this_thread::sleep_for(std::chrono::milliseconds(2));check(first.update(0,heavyCaps),"paused update failed");check(bytes(first)==paused,"in-flight result changed paused visible mesh");}
        check(ClothTest::uploads==uploadCount,"pause redundantly uploads SSBO");
        const auto cadenceEpoch=first.currentEpoch();
        first.setFixedSeconds(1.f/240);
        first.setFixedSeconds(0);first.setFixedSeconds(-1);first.setFixedSeconds(std::nanf(""));
        first.setFixedSeconds(std::numeric_limits<float>::infinity());
        for(unsigned i=0;i<20;++i) {
            check(first.update(0,heavyCaps),"paused cadence update failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            check(bytes(first)==paused&&ClothTest::uploads==uploadCount,"cadence change modifies a paused snapshot");
            check(first.currentEpoch()==cadenceEpoch,"cadence change resets the worker generation");
        }
        eventually(first,heavyCaps,[&]{return first.asyncDiagnostics().fixedSeconds==1.f/240;},"new fixed frequency did not reach the persistent worker");
        check(second.asyncDiagnostics().fixedSeconds==1.f/120,"one actor changed another actor's fixed frequency");
        check(first.currentEpoch()==cadenceEpoch,"resuming a new cadence resets physical state");
        geometry(first);
        first.setFixedSeconds(1.f);
        eventually(first,heavyCaps,[&]{return first.asyncDiagnostics().fixedSeconds==1.f/30;},"worker cadence lower bound was not applied");
        first.setFixedSeconds(.000001f);
        eventually(first,heavyCaps,[&]{return first.asyncDiagnostics().fixedSeconds==1.f/480;},"worker cadence upper bound was not applied");
        check(first.currentEpoch()==cadenceEpoch,"bounded cadence change resets physical state");
        first.setFixedSeconds(1.f/120);
        eventually(first,heavyCaps,[&]{return first.asyncDiagnostics().fixedSeconds==1.f/120;},"original worker cadence was not restored");
        std::cout<<"PASS game-time pause/cadence changes freeze snapshot; persistent worker updates frequency without reset\n";

        std::cout<<"PHASE discontinuity resets"<<std::endl;
        for(unsigned change=0;change<4;++change) {
            std::cout<<"PHASE reset "<<change<<std::endl;
            const auto epoch=first.currentEpoch();
            if(change==0)fixture.first.world=glm::translate(glm::mat4(1),glm::vec3(30,0,0));
            if(change==1)fixture.first.world=glm::scale(fixture.first.world,glm::vec3(3));
            if(change==2)fixture.first.world=glm::rotate(fixture.first.world,glm::radians(180.f),glm::vec3(0,1,0));
            if(change==3)fixture.first.world=glm::scale(fixture.first.world,glm::vec3(-1,1,1));
            check(first.update(1.f/120,{}),"reset submission failed");check(first.currentEpoch()>epoch,"discontinuity did not invalidate generation");
            eventually(first,{},[&]{return first.outputReady()&&first.asyncDiagnostics().epoch==first.currentEpoch();},"reset generation was dropped");
            check(same(first.snapshotWorld(),fixture.first.world),"old transform accepted after reset");geometry(first);
        }
        const auto gradualEpoch=first.currentEpoch();
        for(unsigned i=0;i<6;++i) {fixture.first.world=glm::scale(fixture.first.world,glm::vec3(1.0005f));check(first.update(1.f/120,{}),"gradual scale input failed");}
        check(first.currentEpoch()>gradualEpoch,"small uniform scale drift never reset material lengths");
        eventually(first,{},[&]{return first.outputReady()&&first.asyncDiagnostics().epoch==first.currentEpoch();},"gradual scale generation missing");
        std::cout<<"PASS teleport/3x/rotation/reflection and cumulative tiny scaling reject old generations\n";

        std::cout<<"PHASE hide/reappear"<<std::endl;
        first.setActive(false);const auto hidden=bytes(first);const auto hiddenUploads=ClothTest::uploads;
        for(unsigned i=0;i<25;++i){check(first.update(1.f/60,heavyCaps),"hidden update failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(bytes(first)==hidden&&ClothTest::uploads==hiddenUploads,"inactive cloth consumes or uploads work");
        first.setActive(true);check(!first.outputReady(),"reappearance displays pre-hide geometry");
        first.reset();check(first.update(1.f/120,{}),"explicit reset failed");const auto resetEpoch=first.currentEpoch();
        for(unsigned i=0;i<20;++i)check(first.update(1.f/120,{}),"coalesced reset publication failed");
        eventually(first,{},[&]{return first.outputReady()&&first.asyncDiagnostics().epoch==resetEpoch;},"coalescing erased reset");
        std::cout<<"PASS hidden worker is suspended/reappearance invalidates/explicit reset survives overwritten requests\n";

        std::cout<<"PHASE invalid capture recovery"<<std::endl;
        const auto validWorld=fixture.first.world;fixture.first.world[0][0]=std::nanf("");
        check(!first.update(1.f/120,{}),"invalid capture accepted");check(!first.outputReady(),"invalid capture leaves obsolete output drawable");
        fixture.first.world=validWorld;
        eventually(first,{},[&]{return first.outputReady();},"valid input did not recover from rejected capture");
        std::cout<<"PASS invalid transform is rejected before publication and valid input recovers\n";

        std::cout<<"PHASE in-flight instance shutdown"<<std::endl;
        const auto firstBuffer=first.getBufferID(0),secondBuffer=second.getBufferID(0);
        check(first.update(1.f/120,heavyCaps),"shutdown job failed");std::cout<<"PHASE join first worker"<<std::endl;first.release();std::cout<<"PHASE first joined"<<std::endl;
        check(!ClothTest::buffers.count(firstBuffer)&&ClothTest::buffers.count(secondBuffer),"joining one instance damages other buffers");
        eventually(second,{},[&]{return second.outputReady();},"other instance failed after release");geometry(second);std::cout<<"PHASE join second worker"<<std::endl;second.release();std::cout<<"PHASE second joined"<<std::endl;
        check(ClothTest::buffers.empty(),"SSBO leak after worker joins");
        std::cout<<"PASS in-flight shutdown joins before mesh/GL release and other instance survives\n";

        std::cout<<"PHASE worker allocation failure fixture"<<std::endl;
        Ge::SkinnedCloth failing;check(failing.bind(fixture.mesh,&fixture.second,settings)&&failing.startAsync(),"failure fixture did not start");
        std::cout<<"PHASE deny one large worker allocation; publish failure job"<<std::endl;
        failWorkerAllocation.store(true,std::memory_order_relaxed);check(failing.update(1.f/120,heavyCaps),"failure request was not published");
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        while(!failing.workerFailed()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        failWorkerAllocation.store(false,std::memory_order_relaxed);
        std::cout<<"PHASE allocation failure reported="<<failing.workerFailed()<<"; joining"<<std::endl;
        check(failing.workerFailed(),"worker allocation exception escaped or was not reported");check(!failing.update(1.f/120,{}),"failed worker update silently succeeds");failing.release();std::cout<<"PHASE failed worker joined"<<std::endl;
        check(ClothTest::buffers.empty()&&ClothTest::foreignCalls.load()==0,"worker touched scene/GL or leaked buffers");
        std::cout<<"PASS worker allocation exception is contained/reported; no scene/GL access outside main thread\n";
    }catch(const std::exception& error) {failWorkerAllocation.store(false);std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
