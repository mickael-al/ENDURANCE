// Private GPU companion to ProceduralTerrainGpu.cpp. Kept in its translation
// unit so standalone backend/benchmark builds use the same implementation.
namespace ProceduralTerrain
{
    class ObjectPlacementGpu
    {
        struct SectorKey
        {
            std::array<std::int32_t,3> origin{};
            std::uint32_t seed=0;
            bool operator<(const SectorKey& b) const noexcept
            { return origin<b.origin || (origin==b.origin && seed<b.seed); }
        };
        // An aligned octree chunk is one contiguous Morton interval. Keeping
        // disjoint intervals avoids a 32-KiB CPU bitmap per coverage/claim at LOD6.
        using Range=std::pair<std::uint32_t,std::uint32_t>; // [first,last)
        using Mask=std::vector<Range>;
        using SharedMask=std::shared_ptr<const Mask>;
        static void normalizeMask(Mask& mask)
        {
            std::sort(mask.begin(),mask.end()); std::size_t count=0;
            for(const auto range:mask)
                if(count && range.first<=mask[count-1].second) mask[count-1].second=(std::max)(mask[count-1].second,range.second);
                else mask[count++]=range;
            mask.resize(count);
        }
        static void addMask(Mask& target,const Mask& added)
        { target.insert(target.end(),added.begin(),added.end()); normalizeMask(target); }
        static Mask subtractMask(const Mask& input,const Mask& removed)
        {
            Mask result; std::size_t cursor=0;
            for(const auto range:input)
            {
                auto first=range.first;
                while(cursor<removed.size() && removed[cursor].second<=first) ++cursor;
                for(auto i=cursor;i<removed.size() && removed[i].first<range.second;++i)
                {
                    if(removed[i].first>first) result.emplace_back(first,removed[i].first);
                    first=(std::max)(first,removed[i].second);
                    if(first>=range.second) break;
                }
                if(first<range.second) result.emplace_back(first,range.second);
            }
            return result;
        }
        static bool containsMask(const Mask& available,const Mask& wanted)
        {
            std::size_t cursor=0;
            for(const auto range:wanted)
            {
                while(cursor<available.size() && available[cursor].second<=range.first) ++cursor;
                if(cursor==available.size() || available[cursor].first>range.first || available[cursor].second<range.second) return false;
            }
            return true;
        }
        Range coverageRange(const std::array<std::int32_t,3>& origin,unsigned lod,const SectorKey& sector) const
        {
            std::uint32_t first=0;
            for(unsigned axis=0;axis<3;++axis)
            {
                const auto q=static_cast<unsigned>((origin[axis]-sector.origin[axis])/static_cast<std::int32_t>(cells));
                for(unsigned bit=0;bit<config.maximumLod;++bit) first|=((q>>bit)&1u)<<(3*bit+axis);
            }
            return {first,first+(1u<<(3*lod))};
        }
        struct OwnedBlock { ObjectBlock block; SharedMask mask; };
        struct Sector
        {
            Mask captured,reserved,covered,nextCovered;
            bool everPublished=false, discardWhenIdle=false, inRange=true;
            std::vector<OwnedBlock> blocks;
        };
        struct Claim
        {
            SectorKey key; SharedMask mask; std::uint32_t sites=0;
            std::array<std::int32_t,3> origin{};
            std::uint32_t extent=0;
        };
        struct Retired { GLsync fence=nullptr; std::vector<OwnedBlock> blocks; };
        ObjectPlacementConfig config;
        ObjectPlacementStats statistics;
        std::map<SectorKey,Sector> sectors;
        std::vector<Claim> claims;
        std::vector<Retired> retired;
        std::vector<std::vector<std::uint32_t>> freePages;
        std::vector<ObjectBlock> published, publicationScratch;
        using CoverageKey=std::pair<unsigned,std::array<std::int32_t,3>>;
        std::vector<CoverageKey> coarseCoverage;
        bool publicationDirty=true;
        std::uint64_t nextAllocationRevision=1;
        GLuint work=0, counts=0, instances=0, compact=0;
        const volatile std::uint32_t* mapped=nullptr;
        GLint compactJob=-1, compactCount=-1;
        std::uint32_t cells=0, span=0, step=0, side=0, sites=0, hitOffset=0, jobStart=0, jobStride=0;
        float spacing=1.0f;
        float largestRadius=0;
        glm::vec3 lastTrim{};
        bool hasTrim=false;
        bool flushPending=false;
        std::string failure;

        std::uint32_t jobBase(unsigned job) const { return jobStart+job*jobStride; }
        static std::uint32_t bits(float v) { std::uint32_t b; std::memcpy(&b,&v,4); return b; }
        bool inRange(const SectorKey& key) const
        {
            if(!hasTrim) return true;
            double distance2=0;
            for(unsigned a=0;a<3;++a)
            {
                const double lo=double(key.origin[a])*spacing,hi=lo+span*spacing;
                const double d=(std::max)(0.0,(std::max)(lo-lastTrim[a],lastTrim[a]-hi)); distance2+=d*d;
            }
            const double limit=double(config.retentionDistance)+cells*spacing+largestRadius;
            return distance2<=limit*limit;
        }
        void returnPages(const std::vector<OwnedBlock>& blocks)
        {
            for(const auto& b:blocks) freePages[b.block.prototype].push_back(b.block.firstInstance);
        }
        void rebuildPublished()
        {
            statistics.sectors=static_cast<std::uint32_t>(sectors.size());
            if(!publicationDirty) return;
            publicationDirty=false; ++statistics.publicationRebuilds;
            publicationScratch.clear(); statistics.instances=0;
            for(const auto& item:sectors) if(item.second.inRange) for(const auto& owned:item.second.blocks)
                if(containsMask(item.second.covered,*owned.mask))
                { publicationScratch.push_back(owned.block); statistics.instances+=owned.block.instanceCount; }
            std::sort(publicationScratch.begin(),publicationScratch.end(),[](const ObjectBlock& a,const ObjectBlock& b)
                { return a.prototype<b.prototype || (a.prototype==b.prototype && a.firstInstance<b.firstInstance); });
            if(publicationScratch!=published)
            {
                published.swap(publicationScratch);
                ++statistics.revision;
            }
            statistics.blocks=static_cast<std::uint32_t>(published.size());
        }
    public:
        ~ObjectPlacementGpu() { shutdown(); }
        bool enabled() const noexcept { return work!=0; }
        bool initialize(const GpuConfig& gpu, std::string& error)
        {
            shutdown(); config=gpu.objects; cells=gpu.cells; spacing=gpu.baseSpacing;
            if(!config.enabled) return true;
            const auto reject=[&](const char* message){ error=failure=message; return false; };
            if(config.maximumLod>6 || config.maximumSectors==0 || config.maximumSectors>65536 ||
                config.prototypes.empty() || config.prototypes.size()>255 || config.groups.size()>255 ||
                !finiteFieldValue(config.candidateSpacing) || config.candidateSpacing<=0 ||
                !finiteFieldValue(config.minimumDistance) || config.minimumDistance<0 ||
                !finiteFieldValue(config.retentionDistance) || config.retentionDistance<=0)
                return reject("Invalid object placement settings");
            span=cells<<config.maximumLod;
            // Power-of-two grids nest exactly, including negative coordinates.
            // A coarse voxel can contain several fixed world candidate cells.
            step=1u<<(std::min)(config.maximumLod,2u);
            const unsigned maximumSide=1u<<(std::min)(config.maximumLod+2u,7u);
            while(step<span && (step*spacing<config.candidateSpacing || span/step>maximumSide)) step*=2;
            if(config.minimumDistance>step*spacing)
                return reject("Object minimum distance must not exceed the effective candidate spacing");
            side=span/step; sites=side*side*(1u<<(std::min)(config.maximumLod,2u));
            const unsigned maskWords=((1u<<(3*config.maximumLod))+31u)/32u;
            hitOffset=(8u+maskWords+7u)/8u*8u;
            std::uint64_t totalCapacity=0;
            for(const auto& p:config.prototypes)
            {
                if(p.capacity<ObjectPageSize || p.capacity%ObjectPageSize ||
                    !finiteFieldValue(p.boundingRadius) || p.boundingRadius<0 ||
                    !finiteFieldValue(p.minimumScale) || !finiteFieldValue(p.maximumScale) ||
                    p.minimumScale<=0 || p.maximumScale<p.minimumScale)
                    return reject("Invalid object prototype capacity, bounds or scale");
                totalCapacity+=p.capacity;
                largestRadius=(std::max)(largestRadius,p.boundingRadius);
            }
            for(const auto& g:config.groups)
                if(g.count==0 || g.first>=config.prototypes.size() || g.count>config.prototypes.size()-g.first)
                    return reject("Invalid object prototype group");
            for(const auto& p:config.biomes)
            {
                const float values[]={p.density,p.minimumNormalY,p.forestPeriod,p.forestThreshold,p.forestSoftness,p.minimumAboveSea};
                for(float v:values) if(!finiteFieldValue(v)) return reject("Non-finite object biome profile");
                if(p.density<0 || p.density>1 || p.minimumNormalY<=0 || p.minimumNormalY>1 ||
                    p.forestPeriod<=0 || p.forestSoftness<=0)
                    return reject("Invalid object biome profile");
                for(unsigned i=0;i<4;++i)
                {
                    const auto g=(p.groups>>(8*i))&255u;
                    if(g!=ObjectEmptyType && g>=config.groups.size()) return reject("Unknown object group in biome profile");
                }
            }
            // Forced empty biomes need neither pools nor a collection variant.
            // Normal biome transitions are still resolved from the GPU columns.
            if(gpu.field.biomeOverride>=0)
            {
                const auto& profile=config.biomes[static_cast<unsigned>(gpu.field.biomeOverride)];
                if(profile.density==0 || profile.weights==0) return true;
            }
            std::vector<std::uint32_t> table(16,0);
            table[0]=static_cast<std::uint32_t>(config.prototypes.size()); table[1]=static_cast<std::uint32_t>(config.groups.size());
            table[2]=step; table[3]=side; table[4]=span; table[5]=cells; table[6]=1u<<config.maximumLod;
            table[7]=sites; table[8]=bits(spacing); table[9]=bits(config.minimumDistance/(step*spacing));
            table[12]=static_cast<std::uint32_t>(table.size());
            for(const auto& p:config.biomes)
                table.insert(table.end(),{p.groups,p.weights,bits(p.density),bits(p.minimumNormalY),bits(p.forestPeriod),
                    bits(p.forestThreshold),bits(p.forestSoftness),bits(p.minimumAboveSea)});
            table[13]=static_cast<std::uint32_t>(table.size());
            for(const auto& g:config.groups) table.insert(table.end(),{g.first,g.count});
            table[14]=static_cast<std::uint32_t>(table.size());
            table.insert(table.end(),BiomeMatrix2D.begin(),BiomeMatrix2D.end());
            table[15]=hitOffset;
            jobStart=static_cast<std::uint32_t>((table.size()+7)/8*8);
            // At most one winning hit exists per site, across ALL prototypes.
            // Compaction groups these hits with a shared histogram; reserving
            // sites positions per prototype multiplies scratch without adding
            // capacity. Keep a single packed site-sized output per job.
            jobStride=hitOffset+sites+2*sites;
            table[10]=jobStride; table[11]=jobStart;
            const auto workBytes=(std::uint64_t{jobStart}+std::uint64_t{jobStride}*gpu.maxInFlight)*4;
            const auto countBytes=config.prototypes.size()*gpu.maxInFlight*4;
            GLint64 maximum=0; glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&maximum);
            if(totalCapacity>UINT32_MAX || totalCapacity*8>static_cast<std::uint64_t>(maximum) ||
                workBytes>static_cast<std::uint64_t>(maximum)) return reject("Object pools exceed the GPU storage block limit");
            const std::string compactDefines="#define TERRAIN_OBJECT_TYPE_COUNT "+std::to_string(config.prototypes.size())+"\n";
            compact=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,gpu.shaderDirectory+"/objects_compact.C.glsl",compactDefines}},
                error,"Terrain object position compaction",Ge::ShaderCacheOptions{true,{}});
            if(!compact) return false;
            compactJob=glGetUniformLocation(compact,"uObjectJobBase"); compactCount=glGetUniformLocation(compact,"uObjectCountBase");
            glCreateBuffers(1,&work); glNamedBufferData(work,static_cast<GLsizeiptr>(workBytes),nullptr,GL_DYNAMIC_DRAW);
            glNamedBufferSubData(work,0,static_cast<GLsizeiptr>(table.size()*4),table.data());
            glCreateBuffers(1,&counts);
            glNamedBufferStorage(counts,static_cast<GLsizeiptr>(countBytes),nullptr,
                GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT|GL_DYNAMIC_STORAGE_BIT);
            mapped=static_cast<const volatile std::uint32_t*>(glMapNamedBufferRange(counts,0,static_cast<GLsizeiptr>(countBytes),
                GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT));
            glCreateBuffers(1,&instances); glNamedBufferData(instances,static_cast<GLsizeiptr>(totalCapacity*8),nullptr,GL_DYNAMIC_DRAW);
            if(!mapped || glGetError()!=GL_NO_ERROR) { shutdown(); return reject("Cannot allocate terrain object pools"); }
            freePages.resize(config.prototypes.size());
            std::uint32_t base=0;
            for(std::size_t i=0;i<config.prototypes.size();++i)
            {
                for(std::uint32_t p=config.prototypes[i].capacity;p>0;p-=ObjectPageSize) freePages[i].push_back(base+p-ObjectPageSize);
                base+=config.prototypes[i].capacity;
            }
            claims.resize(gpu.maxInFlight);
            statistics.poolBytes=totalCapacity*8; statistics.scratchBytes=workBytes+countBytes;
            return true;
        }
        void shutdown() noexcept
        {
            for(auto& r:retired) if(r.fence) glDeleteSync(r.fence);
            if(mapped && counts) glUnmapNamedBuffer(counts);
            const GLuint buffers[]={work,counts,instances};
            if(work || counts || instances) glDeleteBuffers(3,buffers);
            if(compact) glDeleteProgram(compact);
            work=counts=instances=compact=0; mapped=nullptr;
            sectors.clear(); retired.clear(); claims.clear(); freePages.clear(); published.clear(); publicationScratch.clear();
            coarseCoverage.clear(); publicationDirty=true;
            statistics={}; flushPending=false; hasTrim=false; failure.clear();
            largestRadius=0;
        }
        // Called only after a non-empty mesh count and before regular emission.
        // Overlapping speculative jobs reserve disjoint finest-chunk masks.
        std::uint32_t begin(unsigned job,const ChunkRequest& request)
        {
            if(!enabled() || request.lod>config.maximumLod || !failure.empty()) return 0;
            SectorKey key; key.seed=request.seed;
            for(unsigned a=0;a<3;++a) key.origin[a]=objectFloorMultiple(request.origin[a],span);
            auto found=sectors.find(key);
            if(found==sectors.end())
            {
                if(sectors.size()>=config.maximumSectors) { ++statistics.sectorFailures; return 0; }
                found=sectors.emplace(key,Sector{}).first;
                found->second.inRange=inRange(key);
            }
            auto& sector=found->second;
            if(sector.discardWhenIdle) return 0;
            const auto range=coverageRange(request.origin,request.lod,key);
            auto remaining=subtractMask(subtractMask(Mask{range},sector.captured),sector.reserved);
            if(remaining.empty()) { ++statistics.cachedJobs; return 0; }
            const auto mask=std::make_shared<const Mask>(std::move(remaining));
            const unsigned requestSpan=cells<<request.lod,bandSpan=span/(1u<<(std::min)(config.maximumLod,2u));
            const unsigned jobSide=(std::max)(1u,requestSpan/step);
            const unsigned jobSites=jobSide*jobSide*(std::max)(1u,requestSpan/bandSpan);
            claims[job]={key,mask,jobSites,request.origin,requestSpan}; addMask(sector.reserved,*mask);
            // Upload only the word window touched by this chunk. A fine chunk
            // keeps a tiny header/working set even inside a 4-km sector.
            const unsigned firstWord=range.first/32u,lastWord=(range.second+31u)/32u;
            std::vector<std::uint32_t> header(8u+lastWord-firstWord,0);
            for(unsigned a=0;a<3;++a) header[a]=static_cast<std::uint32_t>(key.origin[a]);
            header[3]=request.seed;
            header[4]=static_cast<unsigned>((request.origin[0]-key.origin[0])/static_cast<int>(step)) |
                (static_cast<unsigned>((request.origin[2]-key.origin[2])/static_cast<int>(step))<<16u);
            header[5]=static_cast<unsigned>((request.origin[1]-key.origin[1])/static_cast<int>(bandSpan))|(firstWord<<16u);
            header[6]=jobSide; header[7]=jobSites;
            for(const auto part:*mask) for(unsigned first=part.first;first<part.second;)
            {
                const unsigned word=first/32u,count=(std::min)(part.second-first,32u-first%32u);
                header[8u+word-firstWord]|=(count==32u?UINT32_MAX:((1u<<count)-1u)<<(first%32u)); first+=count;
            }
            const auto base=jobBase(job); const std::uint32_t zero=0;
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glNamedBufferSubData(work,static_cast<GLintptr>(base)*4,header.size()*4,header.data());
            glClearNamedBufferSubData(work,GL_R32UI,static_cast<GLintptr>(base+hitOffset)*4,jobSites*4,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,work);
            ++statistics.captureJobs;
            return base;
        }
        void compactHits(unsigned job)
        {
            if(!enabled() || !claims[job].mask) return;
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,work);
            // Emission is complete: reuse its counter binding. Stay within the
            // eight SSBO bindings/compute blocks required by desktop OpenGL.
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,counts);
            glUseProgram(compact); glUniform1ui(compactJob,jobBase(job));
            glUniform1ui(compactCount,static_cast<GLuint>(job*config.prototypes.size()));
            // A single group scans only this chunk's active site rectangle.
            // Fine chunks do not scan the entire distant sector. Shared histogram/prefix
            // offsets produce a packed per-type list and overwrite every count,
            // including zero types, without global atomics or a clear pass.
            glDispatchCompute(1,1,1);
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
        }
        // Existing terrain completion fence covers collection + compaction.
        // Only <=255 counters are read; positions remain GPU-to-GPU copies.
        void complete(unsigned job,bool accept)
        {
            if(!enabled() || !claims[job].mask) return;
            const auto claim=claims[job]; claims[job]={};
            auto found=sectors.find(claim.key);
            if(found==sectors.end()) return;
            auto& sector=found->second; sector.reserved=subtractMask(sector.reserved,*claim.mask);
            if(!accept || sector.discardWhenIdle)
            {
                if(sector.reserved.empty() && !sector.everPublished && (sector.discardWhenIdle || sector.blocks.empty()))
                {
                    Retired batch; batch.blocks=std::move(sector.blocks);
                    sectors.erase(found); publicationDirty=true; retire(std::move(batch)); rebuildPublished();
                }
                return;
            }
            std::uint32_t total=0;
            for(std::size_t type=0;type<config.prototypes.size();++type)
            {
                const auto count=mapped[job*config.prototypes.size()+type];
                if(count>claim.sites || total>claim.sites-count || (count+ObjectPageSize-1)/ObjectPageSize>freePages[type].size())
                { ++statistics.capacityFailures; return; }
                total+=count;
            }
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            std::uint32_t typeOffset=0;
            for(std::size_t type=0;type<config.prototypes.size();++type)
            {
                const auto count=mapped[job*config.prototypes.size()+type];
                for(std::uint32_t first=0;first<count;first+=ObjectPageSize)
                {
                    ObjectBlock block;
                    block.prototype=static_cast<std::uint32_t>(type);
                    block.firstInstance=freePages[type].back(); freePages[type].pop_back();
                    block.allocationRevision=nextAllocationRevision++;
                    block.instanceCount=(std::min)(ObjectPageSize,count-first);
                    block.sectorLattice=claim.key.origin; block.seed=claim.key.seed; block.extent=span*spacing;
                    const float radius=config.prototypes[type].boundingRadius;
                    for(unsigned a=0;a<3;++a)
                    {
                        block.origin[a]=static_cast<float>(claim.key.origin[a])*spacing;
                        block.boundsOrigin[a]=static_cast<float>(claim.origin[a])*spacing-radius;
                    }
                    block.boundsExtent=claim.extent*spacing+2*radius;
                    const auto source=std::uint64_t{jobBase(job)+hitOffset+claim.sites}+2*(std::uint64_t{typeOffset}+first);
                    glCopyNamedBufferSubData(work,instances,static_cast<GLintptr>(source*4),
                        static_cast<GLintptr>(block.firstInstance)*8,block.instanceCount*8);
                    sector.blocks.push_back({block,claim.mask});
                }
                typeOffset+=count;
            }
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
            addMask(sector.captured,*claim.mask); statistics.generatedInstances+=total;
            if(total) publicationDirty=true;
        }
        // Called with atomically published terrain leaves, never staged meshes.
        // Fine leaves fill coverage bits; a coarse ancestor covers a whole sector.
        void publish(const std::vector<LodChunk>& chunks,std::uint32_t seed)
        {
            if(!enabled()) return;
            ++statistics.publicationChecks;
            if(sectors.empty()) return;
            for(auto& s:sectors) s.second.nextCovered.clear();
            coarseCoverage.clear(); coarseCoverage.reserve(chunks.size());
            std::uint32_t coarseLods=0;
            for(const auto& chunk:chunks)
            {
                if(chunk.lod>config.maximumLod)
                {
                    if(chunk.lod<=20) { coarseCoverage.emplace_back(chunk.lod,chunk.origin); coarseLods|=1u<<chunk.lod; }
                    continue;
                }
                SectorKey key; key.seed=seed;
                for(unsigned a=0;a<3;++a) key.origin[a]=objectFloorMultiple(chunk.origin[a],span);
                auto found=sectors.find(key);
                if(found!=sectors.end()) found->second.nextCovered.push_back(coverageRange(chunk.origin,chunk.lod,key));
            }
            std::sort(coarseCoverage.begin(),coarseCoverage.end());
            Retired batch;
            for(auto it=sectors.begin();it!=sectors.end();)
            {
                auto& sector=it->second;
                if(it->first.seed==seed) for(unsigned lod=config.maximumLod+1;lod<=20 && coarseLods;++lod)
                {
                    if((coarseLods&(1u<<lod))==0) continue;
                    std::array<std::int32_t,3> origin;
                    for(unsigned a=0;a<3;++a) origin[a]=objectFloorMultiple(it->first.origin[a],cells<<lod);
                    ++statistics.coarseCoverageLookups;
                    if(std::binary_search(coarseCoverage.begin(),coarseCoverage.end(),CoverageKey{lod,origin}))
                    {
                        sector.nextCovered={{0,1u<<(3*config.maximumLod)}};
                        break;
                    }
                }
                normalizeMask(sector.nextCovered);
                if(sector.covered!=sector.nextCovered) publicationDirty=true;
                sector.covered.swap(sector.nextCovered);
                if(!sector.covered.empty()) sector.everPublished=true;
                // Unpublished captures may still belong to the current pending
                // transaction. They are explicitly discarded on abandonment.
                if(sector.covered.empty() && sector.reserved.empty() && sector.everPublished)
                {
                    batch.blocks.insert(batch.blocks.end(),sector.blocks.begin(),sector.blocks.end());
                    it=sectors.erase(it); publicationDirty=true;
                }
                else ++it;
            }
            retire(std::move(batch)); rebuildPublished();
        }
        void discardUnpublished()
        {
            if(!enabled()) return;
            Retired batch;
            for(auto it=sectors.begin();it!=sectors.end();)
            {
                if(!it->second.everPublished)
                {
                    it->second.discardWhenIdle=true;
                    if(it->second.reserved.empty())
                    { batch.blocks.insert(batch.blocks.end(),it->second.blocks.begin(),it->second.blocks.end()); it=sectors.erase(it); publicationDirty=true; continue; }
                }
                ++it;
            }
            retire(std::move(batch)); rebuildPublished();
        }
        void retire(Retired batch)
        {
            if(batch.blocks.empty()) return;
            batch.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            if(!batch.fence) failure="Object retirement fence creation failed; pages remain quarantined";
            retired.push_back(std::move(batch)); flushPending=true;
        }
        void trim(const glm::vec3& camera)
        {
            if(!enabled()) return;
            const auto delta=glm::dvec3(camera)-glm::dvec3(lastTrim);
            const double moveStep=cells*spacing;
            if(hasTrim && glm::dot(delta,delta)<moveStep*moveStep) return;
            lastTrim=camera; hasTrim=true;
            bool changed=false;
            for(auto& item:sectors)
            {
                const bool visible=inRange(item.first);
                if(item.second.inRange!=visible) { item.second.inRange=visible; changed=true; publicationDirty=true; }
            }
            // Keep positions while their terrain is resident, even outside the
            // draw radius. Returning to an unchanged coarse mesh must reveal its
            // trees without re-running terrain generation. Publication/discard
            // releases unloaded or abandoned sectors; the pool remains bounded.
            if(changed) rebuildPublished();
        }

        void poll()
        {
            if(flushPending) { glFlush(); flushPending=false; }
            for(auto it=retired.begin();it!=retired.end();)
            {
                if(!it->fence) { ++it; continue; }
                const auto status=glClientWaitSync(it->fence,0,0);
                if(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED)
                { glDeleteSync(it->fence); returnPages(it->blocks); it=retired.erase(it); }
                else
                {
                    if(status==GL_WAIT_FAILED) failure="Object retirement fence failed; pages remain quarantined";
                    ++it;
                }
            }
        }
        GLuint buffer() const noexcept { return instances; }
        const std::vector<ObjectBlock>& blocks() const noexcept { return published; }
        ObjectPlacementStats stats() const noexcept
        { auto result=statistics; result.sectors=static_cast<std::uint32_t>(sectors.size()); return result; }
        const std::string& error() const noexcept { return failure; }
    };
}
