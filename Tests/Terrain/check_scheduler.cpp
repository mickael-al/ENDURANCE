// Runs the production ProceduralGeneration.cpp update loop and real planning
// worker. Rendering services and GPU completion times are test-only doubles.
#include "scheduler_engine.hpp"
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainCollision.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

namespace Test {
using namespace ProceduralTerrain;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
unsigned frame=0,submittedThisFrame=0,delay=6;
unsigned pollsThisFrame=0,dispatchesThisFrame=0,peakPolls=0,peakDispatches=0;
bool instantGpu=false;
std::uint64_t submissions=0;bool failNext=false,failAfterDispatch=false,invalidRelease=false,holdCompletions=false;
const ProceduralGeneration* failingGenerator=nullptr;
std::vector<LodChunk> coverageAtFailedDispatch;
struct Pending {ChunkRequest request;std::uint64_t token;unsigned readyFrame;bool dispatched=false,cancelled=false,countFinished=false;};
}
namespace ProceduralTerrain {
struct GpuMesher::Impl {
 GpuConfig config;GpuStats stats;std::string error;
 std::vector<Test::Pending> jobs;std::unordered_map<unsigned,MeshHandle> live;unsigned nextSlot=1;
};
GpuMesher::GpuMesher():m_impl(new Impl){} GpuMesher::~GpuMesher()=default;
bool GpuMesher::initialize(const GpuConfig& c,std::string&){m_impl->config=c;m_impl->stats.budgetBytes=std::uint64_t(c.vertexBudgetMiB)<<20;return true;}
void GpuMesher::shutdown()noexcept{m_impl->jobs.clear();m_impl->live.clear();m_impl->stats={};}
bool GpuMesher::submit(const ChunkRequest& request,std::uint64_t token){
 auto& s=*m_impl;if(s.jobs.size()>=s.config.maxInFlight)return false;
 for(const auto& j:s.jobs)Test::require(j.token!=token,"duplicate live token");
 s.jobs.push_back({request,token,Test::frame+Test::delay});
 ++s.stats.pendingJobs;++Test::submittedThisFrame;++Test::submissions;return true;
}
void GpuMesher::dispatchQueued(){
 ++Test::dispatchesThisFrame;for(auto& j:m_impl->jobs)j.dispatched=true;
 if(Test::failAfterDispatch){
  Test::require(Test::failingGenerator!=nullptr,"missing failure fixture owner");
  Test::failingGenerator->getVisibleLodChunks(Test::coverageAtFailedDispatch);
  Test::failAfterDispatch=false;Test::failNext=true;
 }
}
void GpuMesher::poll(std::vector<Completion>& out){
 auto& s=*m_impl;++Test::pollsThisFrame;
#ifdef TERRAIN_SEA_STREAMING
 Test::injectSeaCompletion(out);
#endif
 for(auto it=s.jobs.begin();it!=s.jobs.end();){
  if(Test::holdCompletions || !it->dispatched || it->readyFrame>Test::frame){++it;continue;}
  const bool empty=Test::instantGpu && ((unsigned(it->request.origin[0]/64)
   +unsigned(it->request.origin[1]/64)+unsigned(it->request.origin[2]/64))&3u)==0;
  // Instant GPU still has separate count and emit fences. One poll may start
  // emission, never finish that newly emitted job; empty counts finish early.
  if(Test::instantGpu && !empty && !it->countFinished && !it->cancelled && !Test::failNext){
   it->countFinished=true;++it;continue;
  }
  Completion c;c.token=it->token;
  if(it->cancelled)c.status=CompletionStatus::Cancelled;
  else if(Test::failNext){Test::failNext=false;c.status=CompletionStatus::Failed;c.error="injected GPU failure";++s.stats.failedJobs;}
  else if(empty){c.status=CompletionStatus::Empty;++s.stats.emptyJobs;}
  else {
#ifdef TERRAIN_SEA_STREAMING
   c.hasMarineSurface=(Test::seaMask(it->request,s.config)&(1u<<MarineBiome))!=0;
   if(Test::seaEmpty(it->request,s.config)) {
    c.status=CompletionStatus::Empty;++s.stats.emptyJobs;
   } else
#endif
   {
   c.status=CompletionStatus::Ready;auto& m=c.mesh;m.slot=s.nextSlot++;m.generation=1;m.vertexCount=3;
   m.lod=it->request.lod;m.transitionMask=it->request.transitionMask;
   for(unsigned axis=0;axis<3;++axis)m.origin[axis]=it->request.origin[axis]*s.config.baseSpacing;
   m.extent=float(std::uint64_t(64)<<m.lod)*s.config.baseSpacing;s.live.emplace(m.slot,m);
   ++s.stats.completedJobs;++s.stats.residentChunks;s.stats.residentVertices+=3;s.stats.allocatedBytes+=96;
   }
  }
  out.push_back(c);it=s.jobs.erase(it);--s.stats.pendingJobs;
 }
}
void GpuMesher::cancel(std::uint64_t token)noexcept{for(auto& j:m_impl->jobs)if(j.token==token)j.cancelled=true;}
void GpuMesher::release(const MeshHandle& m)noexcept{
 if(m.slot==UINT32_MAX)return;auto& s=*m_impl;
 if(!s.live.erase(m.slot)){Test::invalidRelease=true;return;}
 --s.stats.residentChunks;s.stats.residentVertices-=3;s.stats.allocatedBytes-=96;
}
bool GpuMesher::valid(const MeshHandle& m)const noexcept{return m_impl->live.count(m.slot)!=0;}
void GpuMesher::drawMany(const std::vector<MeshHandle>& meshes,std::uint32_t){for(const auto& m:meshes)Test::require(valid(m),"drawing retired mesh");}
const GpuConfig& GpuMesher::config()const noexcept{return m_impl->config;}
GpuStats GpuMesher::stats()const noexcept{return m_impl->stats;}
const std::string& GpuMesher::lastError()const noexcept{return m_impl->error;}
void GpuMesher::publishObjectCoverage(const std::vector<LodChunk>&,std::uint32_t){}
void GpuMesher::discardUnpublishedObjects(){}
void GpuMesher::trimObjectSectors(const glm::vec3&){}
unsigned int GpuMesher::objectInstanceBuffer()const noexcept{return 0;}
const std::vector<ObjectBlock>& GpuMesher::objectBlocks()const noexcept{static const std::vector<ObjectBlock> empty;return empty;}
ObjectPlacementStats GpuMesher::objectStats()const noexcept{return {};}
const std::string& GpuMesher::objectError()const noexcept{static const std::string empty;return empty;}
struct CollisionCache::Impl {std::string error;};
CollisionCache::CollisionCache():m_impl(new Impl){}CollisionCache::~CollisionCache()=default;
bool CollisionCache::initialize(Ge::PhysicsWraper*,const CollisionConfig&){return true;}
void CollisionCache::update(const glm::vec3&,const std::vector<MeshHandle>&,const GpuMesher&){}
void CollisionCache::shutdown()noexcept{}CollisionStats CollisionCache::stats()const noexcept{return {};}
const std::string& CollisionCache::lastError()const noexcept{return m_impl->error;}
}

namespace Test {
using Id=std::tuple<std::array<std::int32_t,3>,std::uint32_t>;
std::set<Id> identities(const std::vector<LodChunk>& chunks){std::set<Id> r;for(const auto& c:chunks)require(r.insert({c.origin,c.lod}).second,"duplicate published chunk");return r;}
std::vector<LodChunk> published(const ProceduralGeneration& g){std::vector<LodChunk> r;g.getVisibleLodChunks(r);return r;}
void checkCoverage(const std::vector<LodChunk>& chunks){
 for(std::size_t i=0;i<chunks.size();++i)for(std::size_t j=i+1;j<chunks.size();++j){
  const auto& a=chunks[i];const auto& b=chunks[j];const std::int64_t aw=std::int64_t(64)<<a.lod,bw=std::int64_t(64)<<b.lod;
  std::int64_t overlap[3];for(unsigned k=0;k<3;++k)overlap[k]=std::min(std::int64_t(a.origin[k])+aw,std::int64_t(b.origin[k])+bw)-std::max(std::int64_t(a.origin[k]),std::int64_t(b.origin[k]));
  require(!(overlap[0]>0&&overlap[1]>0&&overlap[2]>0),"published parent/child overlap");
  for(unsigned k=0;k<3;++k)if(overlap[k]==0&&overlap[(k+1)%3]>0&&overlap[(k+2)%3]>0){
   require(std::abs(int(a.lod)-int(b.lod))<=1,"2:1 balance broken");
   const unsigned f=2*k+(std::int64_t(a.origin[k])+aw==b.origin[k]?1:0);
   require(bool(a.transitionMask&(1u<<f))==(a.lod+1==b.lod),"first shared transition face broken");
   require(bool(b.transitionMask&(1u<<(f^1u)))==(b.lod+1==a.lod),"second shared transition face broken");
  }
 }
}
std::set<Id> expected(const glm::vec3& camera,const ProceduralGeneration::Config& c){
 LodConfig l;l.baseSpacing=c.cellSize;l.maxLod=c.maxLod;l.viewDistance=c.viewDistance;l.splitDistanceFactor=c.lodDistanceFactor;l.maxLeaves=c.maxSelectedChunks;l.fullResolutionRadius=0;
 auto bounds=fieldHeightBounds(c.field);
 if(c.field.mode==FieldMode::Terrain){bounds.minimumY=(std::min)(bounds.minimumY,c.field.seaLevel);bounds.maximumY=(std::max)(bounds.maximumY,c.field.seaLevel);}
 const float pad=std::ldexp(c.cellSize,int(c.maxLod));l.minimumY=bounds.minimumY-pad;l.maximumY=bounds.maximumY+pad;
 std::vector<LodChunk> chunks;std::string error;require(selectLodChunks(camera,l,chunks,error),error);return identities(chunks);
}
void step(ProceduralGeneration& g,Ge::Camera& camera,bool allowFailure=false){
 const auto previousPublications=g.getStats().publishedLodGroups;
#ifdef TERRAIN_SEA_STREAMING
 const auto previousWater=g.getSeaTiles();const auto previousRevision=g.getCoverageRevision();
 const auto* previousWaterData=g.getSeaTiles().data();
#endif
 ++frame;submittedThisFrame=pollsThisFrame=dispatchesThisFrame=0;g.prepareRenderer();const auto& s=g.getStats();const auto& c=g.getConfig();
#ifdef TERRAIN_SEA_STREAMING
 checkSea(g,previousWater,previousRevision,previousWaterData);
#endif
 require(submittedThisFrame<=c.maxChunksPerFrame,"per-frame submission budget exceeded");
 require(pollsThisFrame<=2 && dispatchesThisFrame<=2,"unbounded completion polling or batch dispatch");
 require(s.publishedLodGroups-previousPublications<=4,"per-frame publication budget exceeded");
 peakPolls=(std::max)(peakPolls,pollsThisFrame);peakDispatches=(std::max)(peakDispatches,dispatchesThisFrame);
 require(s.pendingJobs<=c.maxInFlight,"in-flight budget exceeded");
 require(s.pendingLodGroups<=1,"more than one frozen GPU transaction");
 require(s.largestLodTransaction<=32,"unbounded transaction");
 require(s.residentChunks<=c.maxResidentChunks && s.allocatedBytes<=s.budgetBytes,"residency budget exceeded");
 require(allowFailure || !s.resourceLimited,"unexpected resource failure: "+g.getLastError());
#ifdef TERRAIN_SCHEDULER_COMPLETION
 require(!s.coverageComplete || (!s.rebuilding && !s.pendingJobs && !s.cpuPlanning
  && !s.generationBlocked && !s.queuedChunks),"incomplete work reported complete");
#endif
 require(!invalidRelease,"mesh released twice");
 checkCoverage(published(g));g.drawRenderer();g.drawShadowRenderer(0);
 // Give the real worker a representative opportunity to prepare its successor.
 // This is test frame pacing, not a measured hardware frame rate.
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
struct Metrics {unsigned frames=0,emptyAfterStart=0,publicationEmpty=0,publicationRefilled=0;std::uint64_t publications=0,submitted=0;};
Metrics settle(ProceduralGeneration& g,Ge::Camera& camera,bool recovering=false){
 Metrics r;bool started=false;const auto startSubmissions=submissions;auto old=g.getStats().publishedLodGroups;
 for(unsigned i=0;i<10000;++i){
  step(g,camera,recovering);++r.frames;const auto& s=g.getStats();started|=s.pendingJobs>0;
  recovering &= s.resourceLimited;
  if(started&&s.rebuilding&&!s.pendingJobs)++r.emptyAfterStart;
  if(s.publishedLodGroups>old){
   r.publications+=s.publishedLodGroups-old;old=s.publishedLodGroups;
   if(s.rebuilding && !s.pendingJobs)++r.publicationEmpty;
   if(s.pendingJobs && submittedThisFrame)++r.publicationRefilled;
  }
  if(!s.rebuilding&&!s.pendingJobs){
#ifdef TERRAIN_SCHEDULER_COMPLETION
   require(s.coverageComplete&&!s.generationBlocked,"converged coverage not reported complete");
#endif
   require(identities(published(g))==expected(camera.getPosition(),g.getConfig()),"wrong final camera coverage");r.submitted=submissions-startSubmissions;return r;}
 }
 throw std::runtime_error("scheduler did not converge");
}
void json(const Metrics& m){std::cout<<"{\"frames\":"<<m.frames<<",\"unfinished_zero_job_frames\":"<<m.emptyAfterStart<<",\"publication_zero_job_frames\":"<<m.publicationEmpty<<",\"publication_refilled_frames\":"<<m.publicationRefilled<<",\"publications\":"<<m.publications<<",\"submitted_chunks\":"<<m.submitted<<"}";}
}

int main(int argc,char** argv){
 try {
  using namespace Test;bool baseline=false;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];
   if(arg=="--baseline")baseline=true;
   else if(arg=="--instant-gpu"){instantGpu=true;delay=0;}
   else throw std::runtime_error("Unknown fixture option: "+arg);
  }
  Ge::GraphiquePipelineManager pipelines;Ge::CameraManager cameras;Ge::LightManager lights;
  auto* engine=Ge::Engine::getPtrClassAddr();engine->graphiquePipelineManager=&pipelines;engine->cameraManager=&cameras;engine->lightManager=&lights;
  auto& camera=cameras.camera;camera.setPosition({1,18,3});SceneData scene;
  ProceduralGeneration::Config config;config.field.mode=FieldMode::Plane;config.field.baseHeight=0;config.maxLod=3;config.viewDistance=512;config.maxChunksPerFrame=2;config.maxInFlight=4;config.maxSelectedChunks=1024;config.maxResidentChunks=2048;config.collisionEnabled=false;
  if(instantGpu){config.maxChunksPerFrame=16;config.maxInFlight=3;}
#ifdef TERRAIN_SEA_STREAMING
  config.field.mode=FieldMode::Terrain;config.field.heightAmplitude=0;config.field.detailScale=0;
#endif
  ProceduralGeneration g(scene,1337);require(g.initialize(config),g.getLastError());
  const Metrics initial=settle(g,camera);
#ifdef TERRAIN_SEA_STREAMING
  checkInitialSea(g);
#endif
  if(!baseline)require(initial.publicationRefilled>0,"successor never submitted on publication frame");
  const auto idleSubmissions=submissions,idleGroups=g.getStats().publishedLodGroups,idleSelections=g.getStats().lodSelections;
  for(unsigned i=0;i<25;++i)step(g,camera);
  require(submissions==idleSubmissions&&g.getStats().publishedLodGroups==idleGroups&&g.getStats().lodSelections==idleSelections,"idle scheduling restarted work");

  if(instantGpu){
   // Fail work dispatched in this update. The extra poll must invalidate its
   // target before a subsequent publication/refill can consume that result.
   failAfterDispatch=true;failingGenerator=&g;
   camera.setPosition({609,18,-527});
   for(unsigned i=0;i<1000&&!g.getStats().resourceLimited;++i)step(g,camera,true);
   require(g.getStats().resourceLimited&&!failAfterDispatch,"post-dispatch failure not reported");
   for(unsigned i=0;i<25;++i)step(g,camera,true);
   require(identities(published(g))==identities(coverageAtFailedDispatch),
           "post-dispatch failure changed published coverage");
   failingGenerator=nullptr;
   camera.setPosition({1,18,3});settle(g,camera,true);
  }

  // Camera changes during active work must not publish mismatched speculative coverage.
  for(unsigned i=0;i<48;++i){if(i%8==0)camera.setPosition({float(160*(int(i/8)%3-1)),18,float(100*(int(i/8)%2))});step(g,camera);}
  camera.setPosition({417,18,-143});const Metrics moved=settle(g,camera);

  // A failed active transaction must keep the previous coverage and invalidate
  // a future plan derived from a state that was never published.
  holdCompletions=true;camera.setPosition({-321,18,273});
  for(unsigned i=0;i<1000 && !g.getStats().pendingJobs;++i)step(g,camera);
  require(g.getStats().pendingJobs>0,"failure fixture never submitted work");
#ifdef TERRAIN_SCHEDULER_LOOKAHEAD
  for(unsigned i=0;i<1000&&!g.getStats().lookaheadReady;++i)step(g,camera);
  require(g.getStats().lookaheadReady,"worker did not prepare a successor during held GPU work");
  const auto beforeMoveDiscard=g.getStats().stalePlansDiscarded;
  camera.setPosition({-417,18,385});step(g,camera);
  require(g.getStats().stalePlansDiscarded>beforeMoveDiscard,"camera movement retained an obsolete successor");
  for(unsigned i=0;i<1000&&!g.getStats().lookaheadReady;++i)step(g,camera);
  require(g.getStats().lookaheadReady,"worker did not refresh obsolete successor");
  const auto beforeFailureDiscard=g.getStats().stalePlansDiscarded;
#endif
  const auto beforeFailure=identities(published(g));failNext=true;holdCompletions=false;
  for(unsigned i=0;i<1000 && !g.getStats().resourceLimited;++i)step(g,camera,true);
  require(g.getStats().resourceLimited,"injected failure was not reported");
  for(unsigned i=0;i<25;++i)step(g,camera,true);
  require(identities(published(g))==beforeFailure,"failed transaction published speculative successor");
  require(g.getStats().pendingJobs==0,"cancelled jobs did not drain");
#ifdef TERRAIN_SCHEDULER_COMPLETION
  require(g.getStats().generationBlocked&&!g.getStats().coverageComplete,
          "failed drained generation was mistaken for completed coverage");
#endif
#ifdef TERRAIN_SCHEDULER_LOOKAHEAD
  require(g.getStats().stalePlansDiscarded>beforeFailureDiscard&&!g.getStats().lookaheadReady,
          "failure did not invalidate the prepared successor");
#endif
  camera.setPosition({49,18,97});const Metrics recovered=settle(g,camera,true);
  require(!g.getStats().resourceLimited,"camera change did not recover after failure");
#ifdef TERRAIN_SCHEDULER_LOOKAHEAD
  require(g.getStats().lookaheadPlansUsed>0,"lookahead never used");
  require(g.getStats().sameFrameHandoffs>0,"same-frame handoff counter did not advance");
  require(!g.getStats().cpuPlanning&&!g.getStats().lookaheadReady,"converged scheduler retains speculative work");
#endif
  std::cout<<"{\"initial\":";json(initial);std::cout<<",\"moved\":";json(moved);std::cout<<",\"recovered\":";json(recovered);
#ifdef TERRAIN_SCHEDULER_LOOKAHEAD
  const auto& s=g.getStats();
  std::cout<<",\"lookahead_plans_used\":"<<s.lookaheadPlansUsed<<",\"same_frame_handoffs\":"<<s.sameFrameHandoffs
           <<",\"stale_plans_discarded\":"<<s.stalePlansDiscarded<<",\"planning_wait_frames\":"<<s.planningWaitFrames;
#endif
  std::cout<<",\"peak_polls_per_update\":"<<peakPolls<<",\"peak_dispatches_per_update\":"<<peakDispatches;
  std::cout<<",\"checks\":[\"actual update/worker\",\"submission budgets\",\"bounded polls/dispatches\",\"published seams\",\"moving camera\",\"failure invalidation\",\"idle stability\"]}"<<'\n';
  g.shutdown();
#ifdef TERRAIN_SEA_STREAMING
  checkStoppedSea(g);
#endif
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL scheduler: "<<e.what()<<'\n';return 1;}
}
