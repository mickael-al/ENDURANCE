#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include "EnduranceObjectPlacement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <tuple>

using namespace ProceduralTerrain;
namespace
{
void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
std::vector<Completion> finish(GpuMesher& gpu,unsigned expected)
{
    std::vector<Completion> output;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    while(output.size()<expected && std::chrono::steady_clock::now()<until)
    { gpu.poll(output); std::this_thread::yield(); }
    require(output.size()==expected,"Terrain jobs did not complete: "+gpu.lastError());
    require(glGetError()==GL_NO_ERROR,"OpenGL error in placement lifecycle"); return output;
}
Completion generate(GpuMesher& gpu,const ChunkRequest& request,std::uint64_t token)
{
    require(gpu.submit(request,token),"Cannot submit terrain: "+gpu.lastError());
    const auto result=finish(gpu,1).front();
    require(result.status==CompletionStatus::Ready || result.status==CompletionStatus::Empty,"Terrain failed: "+result.error);
    return result;
}
struct Point
{
    std::uint32_t type,word0,word1; std::array<std::int32_t,3> origin; float x,y,z;
    bool operator<(const Point& b) const { return std::tie(origin,type,word0,word1)<std::tie(b.origin,b.type,b.word0,b.word1); }
    bool operator==(const Point& b) const { return std::tie(origin,type,word0,word1)==std::tie(b.origin,b.type,b.word0,b.word1); }
};
std::vector<Point> read(GpuMesher& gpu)
{
    std::vector<Point> points;
    for(const auto& b:gpu.objectBlocks())
    {
        require(b.instanceCount>0 && b.instanceCount<=ObjectPageSize,"Invalid block count");
        std::vector<PackedObjectPosition> data(b.instanceCount);
        // Test-only readback. Production exposes GPU storage and metadata only.
        glGetNamedBufferSubData(gpu.objectInstanceBuffer(),b.firstInstance*8,data.size()*8,data.data());
        for(const auto& p:data)
            points.push_back({b.prototype,p.xy,p.zSite,b.sectorLattice,
                b.origin[0]+float(p.xy&65535u)*(b.extent/65535.0f),
                b.origin[1]+float(p.xy>>16)*(b.extent/65535.0f),
                b.origin[2]+float(p.zSite&65535u)*(b.extent/65535.0f)});
    }
    std::sort(points.begin(),points.end()); return points;
}
void checkPlane(const std::vector<Point>& points,float height,float minimum,float tolerance=.003f)
{
    for(std::size_t i=0;i<points.size();++i)
    {
        require(std::abs(points[i].y-height)<tolerance,"Object is not on the emitted terrain surface");
        for(std::size_t j=0;j<i;++j)
        {
            const float dx=points[i].x-points[j].x,dz=points[i].z-points[j].z;
            require(dx*dx+dz*dz>=(minimum-2*tolerance)*(minimum-2*tolerance),"Duplicate or insufficiently spaced sites");
        }
    }
}
std::vector<std::array<std::uint32_t,9>> terrainTriangles(GpuMesher& gpu,const MeshHandle& mesh)
{
    std::vector<std::array<std::uint32_t,9>> triangles(mesh.vertexCount/3);
    glGetNamedBufferSubData(gpu.vertexBuffer(),static_cast<GLintptr>(mesh.firstVertex)*12,
        static_cast<GLsizeiptr>(mesh.vertexCount)*12,triangles.data());
    std::sort(triangles.begin(),triangles.end()); return triangles;
}
GpuConfig config(const char* directory)
{
    GpuConfig c; c.shaderDirectory=directory; c.cells=16; c.maxInFlight=3;
    c.vertexBudgetMiB=16; c.maxResidentChunks=64; c.columnCacheEntries=8; c.densityCacheEntries=8;
    c.field.mode=FieldMode::Plane; c.field.baseHeight=19.5f; c.field.seaLevel=-50; c.field.biomeOverride=12;
    auto& p=c.objects; p.enabled=true; p.maximumLod=2; p.maximumSectors=32;
    p.candidateSpacing=8; p.minimumDistance=3; p.retentionDistance=256;
    p.prototypes={{"a",2048,10,.8f,5},{"b",2048,12,.8f,5},{"c",2048,15,.8f,5}};
    p.groups={{0,3}};
    auto& biome=p.biomes[12]; biome.groups=0xffffff00u; biome.weights=255;
    biome.density=1; biome.forestThreshold=-1; biome.forestSoftness=.1f;
    return c;
}
}
extern "C" int object_placement_tests(const char* shaderDirectory,const char* catalogue)
{
    try
    {
        glewExperimental=GL_TRUE; glewInit(); while(glGetError()!=GL_NO_ERROR) {}
        ObjectPlacementConfig loaded; std::string error;
        require(Endurance::loadObjectPlacement(catalogue,loaded,error),error);
        require(loaded.prototypes.size()==102 && loaded.groups.size()==17,"Baked catalogue is incomplete");
        require(loaded.biomes[0].density==0 && loaded.biomes[25].density==0,"Polar ice and marine must have no trees");
        for(std::size_t i=0;i<loaded.prototypes.size();++i)
            require(loaded.prototypes[i].capacity==65536,"Default per-model pool capacity changed");

        const auto c=config(shaderDirectory); GpuMesher gpu;
        require(gpu.initialize(c,error),error);
        const ChunkRequest root{{0,0,0},2,0,1337};
        const auto mesh=generate(gpu,root,1);
        const auto originalTerrain=terrainTriangles(gpu,mesh.mesh);
        require(gpu.objectBlocks().empty(),"Speculative objects became visible before terrain publication");
        gpu.publishObjectCoverage({{root.origin,root.lod,root.transitionMask}},root.seed);
        const auto original=read(gpu);
        const auto originalRevision=gpu.objectStats().revision;
        const auto originalRebuilds=gpu.objectStats().publicationRebuilds;
        const auto originalBlocks=gpu.objectBlocks();
        for(const auto& b:originalBlocks) require(b.allocationRevision!=0,"Published page lacks a content identity");
        require(original.size()>25,"Expected planted objects on flat eligible ground");
        checkPlane(original,c.field.baseHeight,c.objects.minimumDistance);
        for(unsigned type=0;type<3;++type)
            require(std::any_of(original.begin(),original.end(),[&](const auto& p){return p.type==type;}),"Missing prototype variant");

        const auto captures=gpu.objectStats().captureJobs;
        auto stitched=root; stitched.transitionMask=1;
        const auto seam=generate(gpu,stitched,2);
        gpu.publishObjectCoverage({{stitched.origin,stitched.lod,stitched.transitionMask}},root.seed);
        require(read(gpu)==original && gpu.objectStats().captureJobs==captures,"Transition remeshing regenerated object sites");
        require(gpu.objectStats().revision==originalRevision,"Transition-only terrain invalidated identical tree pages");
        require(gpu.objectStats().publicationRebuilds==originalRebuilds,
            "Transition-only publication rebuilt/sorted an unchanged object list");

        std::vector<LodChunk> fine;
        std::vector<MeshHandle> children;
        for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x)
        {
            ChunkRequest request{{x*32,y*32,z*32},1,0,root.seed};
            children.push_back(generate(gpu,request,10+fine.size()).mesh);
            fine.push_back({request.origin,1,0});
        }
        gpu.publishObjectCoverage(fine,root.seed);
        require(read(gpu)==original && gpu.objectStats().captureJobs==captures,"Refinement changed cached object placement");
        require(gpu.objectStats().revision==originalRevision,"Terrain refinement invalidated unchanged tree buffers");
        require(gpu.objectStats().publicationRebuilds==originalRebuilds,
            "Equivalent fine coverage rebuilt the object list");
        gpu.publishObjectCoverage({{{0,0,0},3,0}},root.seed);
        require(read(gpu)==original,"Coarsening removed existing object sites");
        const auto coarse=generate(gpu,{{0,0,0},3,0,root.seed},30);
        require(gpu.objectStats().captureJobs==captures,"Distant LOD performed object placement work");
        gpu.publishObjectCoverage({{{0,0,0},3,0},{{1024,0,0},4,0}},root.seed);
        gpu.discardUnpublishedObjects();
        require(gpu.objectStats().revision==originalRevision,"Unrelated distant terrain publication rebuilt the tree render/shadow caches");
        const auto stationaryRebuilds=gpu.objectStats().publicationRebuilds;
        const auto stationaryLookups=gpu.objectStats().coarseCoverageLookups;
        for(unsigned i=0;i<64;++i)
            gpu.publishObjectCoverage({{{0,0,0},3,0},{{1024,0,0},4,0}},root.seed);
        require(gpu.objectStats().publicationRebuilds==stationaryRebuilds &&
            gpu.objectStats().coarseCoverageLookups==stationaryLookups+64,
            "Repeated coverage rebuilt positions or searched absent coarse LODs");

        ChunkRequest left{{-64,0,0},2,0,root.seed};
        const auto leftMesh=generate(gpu,left,31);
        gpu.publishObjectCoverage({{root.origin,2,0},{left.origin,2,0}},root.seed);
        const auto combined=read(gpu);
        require(combined.size()>original.size(),"Negative-coordinate neighbour did not generate sites");
        checkPlane(combined,c.field.baseHeight,c.objects.minimumDistance);
        std::vector<ObjectBlock> visible;
        Frustum frustum{}; frustum.planes={glm::dvec4(1,0,0,1000),glm::dvec4(-1,0,0,1000),
            glm::dvec4(0,1,0,1000),glm::dvec4(0,-1,0,1000),glm::dvec4(0,0,1,1000),glm::dvec4(0,0,-1,1000)};
        selectObjectBlocks(gpu.objectBlocks(),frustum,visible);
        require(visible.size()==gpu.objectBlocks().size(),"Frustum dropped visible object blocks");
        frustum.planes[0]=glm::dvec4(1,0,0,-10000); selectObjectBlocks(gpu.objectBlocks(),frustum,visible);
        require(visible.empty(),"Frustum retained fully outside object blocks");

        gpu.publishObjectCoverage({},root.seed);
        require(gpu.objectBlocks().empty() && gpu.objectStats().sectors==0,"Unloaded sectors retained live objects");
        std::vector<Completion> ignored;
        for(int i=0;i<8;++i) gpu.poll(ignored);
        const auto reloaded=generate(gpu,root,40);
        gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        require(read(gpu)==original,"Reloading the same LOD/seed changed deterministic sites");
        const auto reloadedBlocks=gpu.objectBlocks();
        for(const auto& b:reloadedBlocks) for(const auto& old:originalBlocks)
            require(b.allocationRevision!=old.allocationRevision,"Recycled position pages reused stale transform identities");
        const auto beforeCancel=gpu.objectStats().captureJobs;
        require(gpu.submit({{64,0,0},2,0,root.seed},41),"Cancellation submit failed"); gpu.cancel(41);
        require(finish(gpu,1).front().status==CompletionStatus::Cancelled,"Queued cancellation failed");
        require(gpu.objectStats().captureJobs==beforeCancel,"Cancelled queued job allocated placement work");
        require(gpu.submit({{64,0,0},2,0,root.seed},42),"Emission cancellation submit failed");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(gpu.objectStats().captureJobs==beforeCancel && std::chrono::steady_clock::now()<until) gpu.poll(ignored);
        require(gpu.objectStats().captureJobs>beforeCancel,"Emission never started"); gpu.cancel(42);
        require(finish(gpu,1).front().status==CompletionStatus::Cancelled,"Emitting cancellation failed");
        require(gpu.objectStats().sectors==1,"Cancelled placement leaked a sector");
        const auto snapshot=read(gpu); const auto idleCaptures=gpu.objectStats().captureJobs;
        for(int i=0;i<16;++i) gpu.poll(ignored);
        require(read(gpu)==snapshot && gpu.objectStats().captureJobs==idleCaptures,"Idle frames regenerated positions");
        gpu.trimObjectSectors(glm::vec3(10000,0,0));
        require(gpu.objectBlocks().empty() && gpu.objectStats().sectors==1,"Object publication radius failed");
        gpu.trimObjectSectors(glm::vec3(0,0,0));
        require(read(gpu)==snapshot && gpu.objectStats().captureJobs==idleCaptures,
            "Returning to resident terrain lost trees or regenerated their positions");
        gpu.shutdown();

        // Staging capacity depends on total site count, not prototype count.
        // Empty types must still receive a fresh zero counter on reused jobs.
        auto manyTypes=c;
        manyTypes.objects.prototypes.resize(255,c.objects.prototypes.front());
        manyTypes.objects.groups={{0,255}};
        require(gpu.initialize(manyTypes,error),error);
        const auto manyTypeScratch=gpu.objectStats().scratchBytes;
        generate(gpu,root,90); gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        const auto spread=read(gpu);
        require(spread.size()==original.size(),"Prototype compaction changed candidate/site count");
        checkPlane(spread,c.field.baseHeight,c.objects.minimumDistance);
        require(std::any_of(spread.begin(),spread.end(),[](const auto& p){return p.type>200;}),
            "Compaction lost high-numbered prototypes");
        gpu.shutdown();
        require(gpu.initialize(c,error),error);
        require(manyTypeScratch-gpu.objectStats().scratchBytes==(255-c.objects.prototypes.size())*c.maxInFlight*4,
            "Object staging still reserves positions per prototype instead of per site");
        gpu.shutdown();

        // A finer chunk can be the first visible source; filling the rest with
        // a coarser source must neither duplicate nor remove those candidates.
        require(gpu.initialize(c,error),error);
        const ChunkRequest firstChild{{0,0,0},1,0,root.seed};
        generate(gpu,firstChild,43);
        gpu.publishObjectCoverage({{firstChild.origin,1,0}},root.seed);
        require(!read(gpu).empty(),"Initial partial sector has no sites");
        generate(gpu,root,44); gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        require(read(gpu)==original,"Filling a partially captured sector changed or duplicated sites");
        gpu.shutdown();

        // Coarse terrain must populate previously unvisited land, preserving
        // one global lattice when a voxel covers multiple candidate cells.
        auto farConfig=c; farConfig.objects.maximumLod=6; farConfig.objects.retentionDistance=10240;
        farConfig.objects.biomes[12].density=.2f;
        for(auto& p:farConfig.objects.prototypes) p.capacity=32768;
        const ChunkRequest farRoot{{-1024,0,0},6,0,root.seed};
        require(gpu.initialize(farConfig,error),error);
        generate(gpu,farRoot,100); gpu.publishObjectCoverage({{farRoot.origin,6,0}},root.seed);
        const auto farPoints=read(gpu);
        require(farPoints.size()>2000 && !gpu.objectStats().capacityFailures,"LOD6 did not collect all world-grid candidates");
        checkPlane(farPoints,farConfig.field.baseHeight,farConfig.objects.minimumDistance,.02f);
        const auto farCaptures=gpu.objectStats().captureJobs;
        std::vector<LodChunk> farChildren;
        for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x)
        {
            ChunkRequest request{{-1024+x*512,y*512,z*512},5,0,root.seed};
            generate(gpu,request,110+farChildren.size()); farChildren.push_back({request.origin,5,0});
        }
        gpu.publishObjectCoverage(farChildren,root.seed);
        require(read(gpu)==farPoints && gpu.objectStats().captureJobs==farCaptures,"Distant refinement moved or duplicated trees");
        gpu.shutdown();
        // A tiny, offset Morton interval can be captured before its ancestor.
        // The remaining coarse capture must keep precisely the same population.
        const ChunkRequest farPartial{{-64,0,960},2,0,root.seed};
        require(gpu.initialize(farConfig,error),error);
        generate(gpu,farPartial,120); gpu.publishObjectCoverage({{farPartial.origin,2,0}},root.seed);
        require(read(gpu).size()>3,"Offset partial sector fixture has no trees");
        generate(gpu,farRoot,121); gpu.publishObjectCoverage({{farRoot.origin,6,0}},root.seed);
        require(read(gpu)==farPoints,"Partial-to-LOD6 capture changed world sites or overlapped claimed coverage");
        gpu.shutdown();
        require(gpu.initialize(farConfig,error),error);
        require(gpu.submit(farPartial,122) && gpu.submit(farRoot,123),"Concurrent distant claims could not start");
        finish(gpu,2); gpu.publishObjectCoverage({{farRoot.origin,6,0}},root.seed);
        require(read(gpu)==farPoints,"Concurrent coarse/fine claims duplicated or lost sites");
        gpu.shutdown();
        // On first arrival, a fresh 8-km-away chunk is populated. Hiding and
        // returning to it never triggers another field/mesh/placement pass.
        const ChunkRequest distant{{8192,0,0},6,0,root.seed};
        require(gpu.initialize(farConfig,error),error); gpu.trimObjectSectors(glm::vec3(0));
        generate(gpu,distant,124); gpu.publishObjectCoverage({{distant.origin,6,0}},root.seed);
        const auto distantPoints=read(gpu); const auto distantStats=gpu.objectStats();
        require(distantPoints.size()>2000,"New distant terrain has no trees inside the extended radius");
        gpu.trimObjectSectors(glm::vec3(50000,0,0)); require(gpu.objectBlocks().empty(),"Far terrain still publishes tree transforms");
        gpu.trimObjectSectors(glm::vec3(0));
        require(read(gpu)==distantPoints && gpu.objectStats().captureJobs==distantStats.captureJobs,
            "Returning to a resident distant chunk requires regeneration");
        gpu.publishObjectCoverage({},root.seed);
        require(gpu.objectStats().sectors==0 && gpu.objectBlocks().empty(),"Unloaded distant terrain retained its positions");
        gpu.shutdown();

        // Abandonment can race with an in-flight claim in a sector containing
        // completed, but not yet published, positions from a different chunk.
        require(gpu.initialize(c,error),error);
        generate(gpu,{{0,16,0},0,0,root.seed},45);
        const auto stagedCaptures=gpu.objectStats().captureJobs;
        require(gpu.submit({{16,16,0},0,0,root.seed},46),"Staged cancellation submit failed");
        const auto stagedUntil=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(gpu.objectStats().captureJobs==stagedCaptures && std::chrono::steady_clock::now()<stagedUntil) gpu.poll(ignored);
        require(gpu.objectStats().captureJobs>stagedCaptures,"Second staged emission never started");
        gpu.cancel(46); gpu.discardUnpublishedObjects();
        require(finish(gpu,1).front().status==CompletionStatus::Cancelled,"Staged emission did not cancel");
        require(gpu.objectStats().sectors==0 && gpu.objectBlocks().empty(),"Abandoned mixed completed/reserved sector leaked");
        gpu.shutdown();

        for(float exactHeight:{16.0f,64.0f})
        {
            auto integerGround=c; integerGround.field.baseHeight=exactHeight;
            require(gpu.initialize(integerGround,error),error); generate(gpu,root,47);
            gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
            const auto points=read(gpu);
            require(points.size()==original.size(),"Exact cell/chunk-boundary ground lost object sites");
            checkPlane(points,exactHeight,c.objects.minimumDistance); gpu.shutdown();
        }

        auto limited=c; for(auto& p:limited.objects.prototypes) p.capacity=ObjectPageSize;
        require(gpu.initialize(limited,error),error);
        generate(gpu,root,50); gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        const auto safe=read(gpu);
        generate(gpu,left,51); gpu.publishObjectCoverage({{root.origin,2,0},{left.origin,2,0}},root.seed);
        require(gpu.objectStats().capacityFailures>0 && read(gpu)==safe,"Pool overflow damaged existing positions");
        gpu.publishObjectCoverage({},root.seed);
        for(int i=0;i<32;++i) gpu.poll(ignored);
        generate(gpu,left,52); gpu.publishObjectCoverage({{left.origin,2,0}},root.seed);
        require(!read(gpu).empty(),"Retired pages were not reusable");
        gpu.shutdown();

        auto polar=c; polar.field.biomeOverride=0;
        require(gpu.initialize(polar,error),error); generate(gpu,root,60);
        gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        require(gpu.objectBlocks().empty() && gpu.objectStats().captureJobs==0 && gpu.objectStats().poolBytes==0,
            "Forced empty biome performed placement work"); gpu.shutdown();
        auto sea=c; sea.field.seaLevel=100;
        require(gpu.initialize(sea,error),error); generate(gpu,root,61);
        gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        require(gpu.objectBlocks().empty(),"Objects appeared below the configured sea clearance"); gpu.shutdown();

        auto natural=c; natural.field.mode=FieldMode::Terrain; natural.field.biomeOverride=-1;
        natural.field.heightAmplitude=0; natural.field.detailScale=0; natural.field.seaLevel=0;
        for(auto& profile:natural.objects.biomes) { profile=c.objects.biomes[12]; profile.minimumAboveSea=-1000; }
        natural.objects.biomes[MarineBiome].density=0;
        natural.field.baseHeight=63.5f;
        require(gpu.initialize(natural,error),error); generate(gpu,root,64);
        gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        require(read(gpu).size()>25,"Automatic climate selection failed on dry ground"); gpu.shutdown();
        natural.field.baseHeight=-63.5f;
        const ChunkRequest seabed{{0,-64,0},2,0,root.seed};
        require(gpu.initialize(natural,error),error); generate(gpu,seabed,65);
        gpu.publishObjectCoverage({{seabed.origin,2,0}},root.seed);
        require(read(gpu).empty(),"Natural marine biome ignored the column's coastal weight"); gpu.shutdown();

        auto sphere=c; sphere.field.mode=FieldMode::Sphere; sphere.field.sphereCenter={{32,16,32}}; sphere.field.sphereRadius=18;
        require(gpu.initialize(sphere,error),error); generate(gpu,root,62);
        gpu.publishObjectCoverage({{root.origin,2,0}},root.seed);
        const auto cap=read(gpu); require(!cap.empty(),"Sphere top produced no objects");
        for(const auto& p:cap) require(p.y>28,"Slope filter placed an object on a steep wall or underside");
        gpu.shutdown();
        auto disabled=c; disabled.objects.enabled=false;
        require(gpu.initialize(disabled,error),error); const auto ordinaryMesh=generate(gpu,root,63);
        require(terrainTriangles(gpu,ordinaryMesh.mesh)==originalTerrain,"Object collection changed terrain geometry");
        require(gpu.objectInstanceBuffer()==0 && gpu.objectStats().poolBytes==0,"Disabled placement allocated GPU storage");
        gpu.shutdown();
        require(glGetError()==GL_NO_ERROR,"OpenGL error after placement tests");
        std::cout<<"Placement: "<<original.size()<<" flat-ground sites; unchanged terrain, spacing, prototype grouping, integer/negative borders, partial LOD reuse, cancellation, overflow, retirement, masks and culling passed.\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
