#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "glcore.hpp"
#include "TreeRenderGpu.hpp"
#include "ProceduralTreeBake.hpp"
#include "ProceduralTerrainField.hpp"
#include "ShaderCompilation.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace EnduranceTrees
{
namespace
{
using ProceduralTerrain::finiteFieldValue;
constexpr unsigned Page=ProceduralTerrain::ObjectPageSize;
static_assert(Page==64,"Tree culling workgroups match object pages");
struct BlockGpu { glm::vec4 origin; glm::uvec4 info; glm::ivec4 anchor; };
struct ModelData
{
    glm::vec4 centreRadius,scales,impostorCentre;
    glm::uvec4 counts,firstIndices;
    glm::ivec4 baseVertices;
    glm::vec4 crown,crownShape;
};
static_assert(sizeof(BlockGpu)==48 && sizeof(ModelData)==128,"std430 tree layout");
constexpr unsigned InstanceBytes=32,CommandBytes=20,VisibleBytes=8;
constexpr unsigned WindNoiseSize=128;
// Wind derives its weight from local Y; no extra per-vertex/instance attribute.
struct DrawVertex { glm::vec3 position,normal; glm::vec2 uv; glm::vec4 color; };
struct DrawMesh { std::vector<DrawVertex> vertices; std::vector<std::uint32_t> indices; };
static_assert(sizeof(DrawVertex)==48,"Runtime tree vertex layout");
DrawMesh packMesh(const Mesh& mesh)
{
    DrawMesh result; result.vertices.reserve(mesh.vertices.size()); result.indices=mesh.indices;
    for(const auto& v:mesh.vertices) result.vertices.push_back({v.position,v.normal,v.uv,v.color});
    return result;
}

struct State
{
    GLint program=0,vao=0,storage=0,indirect=0,active=0,cullMode=0;
    bool culling=false,graphics=false;
    std::array<bool,4> textureUsed{};
    static constexpr std::array<GLuint,6> indices{{1,2,4,5,6,7}};
    std::array<bool,6> used{};
    std::array<GLint,6> buffers{}; std::array<GLint64,6> starts{},sizes{};
    std::array<GLint,4> textures{},samplers{};
    // Build touches only compute bindings; cached draws only three SSBO slots.
    // Culling and drawing together touch all six. Preserve every changed state
    // without querying/restoring unrelated targets, including SSBO zero.
    explicit State(bool draw=false,bool cull=false,bool normals=true,bool wind=false):graphics(draw),textureUsed{{true,true,normals,wind}}
    {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage);
        for(unsigned i=0;i<indices.size();++i)
        {
            used[i]=draw?(cull || indices[i]==4 || indices[i]==6 || indices[i]==7):
                (indices[i]==2 || indices[i]==4 || indices[i]==5 || indices[i]==6);
            if(!used[i]) continue;
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,indices[i],&buffers[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,indices[i],&starts[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,indices[i],&sizes[i]);
        }
        if(!graphics) return;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING,&indirect);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);
        culling=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
        for(unsigned i=0;i<textureUsed.size();++i) if(textureUsed[i])
        {
            glActiveTexture(GL_TEXTURE0+i);
            glGetIntegerv(i==1 || i==2?GL_TEXTURE_BINDING_2D_ARRAY:GL_TEXTURE_BINDING_2D,&textures[i]);
            glGetIntegeri_v(GL_SAMPLER_BINDING,i,&samplers[i]);
        }
    }
    ~State()
    {
        for(unsigned i=0;i<indices.size();++i)
            if(!used[i]) continue;
            else if(buffers[i] && sizes[i]>0) glBindBufferRange(GL_SHADER_STORAGE_BUFFER,indices[i],buffers[i],starts[i],sizes[i]);
            else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,indices[i],buffers[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,storage); glUseProgram(program);
        if(!graphics) return;
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER,indirect);
        for(unsigned i=0;i<textureUsed.size();++i) if(textureUsed[i])
        {
            glActiveTexture(GL_TEXTURE0+i); glBindTexture(i==1 || i==2?GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D,textures[i]);
            glBindSampler(i,samplers[i]);
        }
        glActiveTexture(active); glBindVertexArray(vao);
        glCullFace(cullMode); if(culling) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    }
};
// Texture uploads must not interpret pointers as PBO offsets or inherit a
// caller's row/skip settings. DSA alone does not isolate pixel unpack state.
struct UnpackState
{
    GLint buffer=0; std::array<GLint,6> values{};
    static constexpr std::array<GLenum,6> names{{GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_IMAGES}};
    UnpackState()
    {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(unsigned i=0;i<names.size();++i) { glGetIntegerv(names[i],&values[i]); glPixelStorei(names[i],i==0?1:0); }
    }
    ~UnpackState()
    { for(unsigned i=0;i<names.size();++i) glPixelStorei(names[i],values[i]); glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer); }
};
struct Buffer
{
    GLuint id=0; std::size_t capacity=0;
    bool allocate(std::size_t bytes,const void* data=nullptr,bool preserve=false,bool grow=false)
    {
        if(!bytes) return true;
        if(!id) glCreateBuffers(1,&id);
        if(bytes>capacity)
        {
            std::size_t next=bytes;
            if(grow) { next=4096; while(next<bytes) next*=2; }
            GLuint replacement=id;
            if(preserve && capacity) glCreateBuffers(1,&replacement);
            glNamedBufferData(replacement,static_cast<GLsizeiptr>(next),nullptr,GL_DYNAMIC_DRAW);
            GLint64 actual=0; glGetNamedBufferParameteri64v(replacement,GL_BUFFER_SIZE,&actual);
            if(actual!=static_cast<GLint64>(next)) { if(replacement!=id) glDeleteBuffers(1,&replacement); return false; }
            if(replacement!=id)
            {
                glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                glCopyNamedBufferSubData(id,replacement,0,0,static_cast<GLsizeiptr>(capacity));
                glDeleteBuffers(1,&id); id=replacement;
            }
            capacity=next;
            if(data) glNamedBufferSubData(id,0,static_cast<GLsizeiptr>(bytes),data);
        }
        else if(data) glNamedBufferSubData(id,0,static_cast<GLsizeiptr>(bytes),data);
        return id!=0;
    }
    void clear() { if(id) glDeleteBuffers(1,&id); id=0; capacity=0; }
};
struct MeshGpu
{
    GLuint vao=0; Buffer vertices,indices; std::uint32_t count=0;
    bool upload(const DrawMesh& mesh)
    {
        if(mesh.indices.empty()) return true;
        if(mesh.indices.size()>static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)())) return false;
        glCreateVertexArrays(1,&vao);
        if(!vao || !vertices.allocate(mesh.vertices.size()*sizeof(DrawVertex),mesh.vertices.data()) ||
            !indices.allocate(mesh.indices.size()*4,mesh.indices.data())) return false;
        glVertexArrayVertexBuffer(vao,0,vertices.id,0,sizeof(DrawVertex)); glVertexArrayElementBuffer(vao,indices.id);
        const GLuint offsets[]={offsetof(DrawVertex,position),offsetof(DrawVertex,normal),offsetof(DrawVertex,uv),offsetof(DrawVertex,color)};
        const GLint sizes[]={3,3,2,4};
        for(GLuint i=0;i<4;++i)
        { glEnableVertexArrayAttrib(vao,i); glVertexArrayAttribFormat(vao,i,sizes[i],GL_FLOAT,GL_FALSE,offsets[i]); glVertexArrayAttribBinding(vao,i,0); }
        count=static_cast<std::uint32_t>(mesh.indices.size()); return true;
    }
    void clear() { if(vao) glDeleteVertexArrays(1,&vao); vao=0; vertices.clear(); indices.clear(); count=0; }
};
using Mips=std::vector<Image>;
struct Asset
{
    Tree tree; std::vector<ImpostorView> views;
    glm::vec3 crownCentre{0},crownInverseExtent{1};
    Mips bark,leaf; std::array<Mips,2> impostors;
    // Both geometric LODs share one draw buffer per material. Commands retain
    // their original index counts, with offsets instead of rebaked indices.
    std::array<DrawMesh,2> meshes;
    glm::uvec4 counts{0},firstIndices{0};
    glm::ivec4 baseVertices{0};
    std::uint64_t bytes=0; std::string error;
    double loadMilliseconds=0;
};
std::uint8_t byte(float value) { return static_cast<std::uint8_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f); }
float linear(float v) { return v<=0.04045f?v/12.92f:std::pow((v+0.055f)/1.055f,2.4f); }
Asset loadAsset(const std::string& path)
{
    Asset asset;
    const auto start=std::chrono::steady_clock::now();
    static const auto decode=[] { std::array<std::uint8_t,256> values{};
        for(unsigned i=0;i<256;++i) values[i]=byte(linear(i/255.0f)); return values; }();
    try
    {
        if(!loadBakedTree(path,asset.tree,asset.error,&asset.views)) return asset;
        // Reuse existing bakes. Compute the macro canopy once, before releasing
        // source meshes; both geometric LODs use this identical reference frame.
        const auto& foliage=asset.tree.lods[0].foliage.vertices;
        glm::vec3 lo=asset.tree.boundsMin,hi=asset.tree.boundsMax;
        if(!foliage.empty())
        {
            lo=hi=foliage.front().position;
            for(const auto& v:foliage) { lo=glm::min(lo,v.position); hi=glm::max(hi,v.position); }
        }
        asset.crownCentre=(lo+hi)*0.5f;
        asset.crownInverseExtent=1.0f/glm::max((hi-lo)*0.5f,glm::vec3(0.01f));
        asset.tree.branches.clear();
        asset.bark=coverageMipmaps(asset.tree.barkTexture); asset.leaf=coverageMipmaps(asset.tree.leafMask);
        const auto addBytes=[&](const Mips& mips){ for(const auto& mip:mips) asset.bytes+=mip.rgba.size(); };
        addBytes(asset.bark); addBytes(asset.leaf);
        for(unsigned material=0;material<2;++material)
        {
            const auto& fine=material?asset.tree.lods[0].foliage:asset.tree.lods[0].wood;
            const auto& coarse=material?asset.tree.lods[1].foliage:asset.tree.lods[1].wood;
            asset.meshes[material].vertices.reserve(fine.vertices.size()+coarse.vertices.size());
            asset.meshes[material].indices.reserve(fine.indices.size()+coarse.indices.size());
        }
        for(unsigned part=0;part<4;++part)
        {
            auto& source=(part&1u)?asset.tree.lods[part/2].foliage:asset.tree.lods[part/2].wood;
            auto& mesh=asset.meshes[part&1u];
            if(mesh.vertices.size()+source.vertices.size()>static_cast<std::size_t>((std::numeric_limits<GLint>::max)()) ||
                mesh.indices.size()+source.indices.size()>static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)()))
                throw std::runtime_error("Baked tree exceeds draw buffer limits");
            asset.counts[part]=static_cast<unsigned>(source.indices.size());
            asset.firstIndices[part]=static_cast<unsigned>(mesh.indices.size());
            asset.baseVertices[part]=static_cast<int>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size()+source.vertices.size());
            for(const auto& v:source.vertices) mesh.vertices.push_back({v.position,v.normal,v.uv,v.color});
            mesh.indices.insert(mesh.indices.end(),source.indices.begin(),source.indices.end());
            source={};
        }
        for(const auto& mesh:asset.meshes) asset.bytes+=mesh.vertices.size()*sizeof(DrawVertex)+mesh.indices.size()*4;
        for(unsigned channel=0;channel<2;++channel) for(unsigned layer=0;layer<asset.views.size();++layer)
        {
            const auto& view=asset.views[layer];
            Image source=channel==0?view.albedo:view.normal;
            if(channel==0) for(std::size_t i=0;i<source.rgba.size();i+=4) for(unsigned c=0;c<3;++c)
                source.rgba[i+c]=decode[source.rgba[i+c]];
            auto mips=coverageMipmaps(source);
            for(auto& mip:mips)
            {
                for(std::size_t i=0;i<mip.rgba.size();i+=4) for(unsigned c=0;c<3;++c)
                    mip.rgba[i+c]=static_cast<std::uint8_t>((unsigned(mip.rgba[i+c])*mip.rgba[i+3]+127u)/255u);
                const auto row=static_cast<std::size_t>(mip.width)*4;
                for(int y=0;y<mip.height/2;++y) std::swap_ranges(mip.rgba.begin()+y*row,mip.rgba.begin()+(y+1)*row,
                    mip.rgba.begin()+(mip.height-1-y)*row);
            }
            addBytes(mips);
            auto& volume=asset.impostors[channel];
            if(volume.empty())
            {
                volume.resize(mips.size());
                for(unsigned level=0;level<mips.size();++level)
                { volume[level].width=mips[level].width; volume[level].height=mips[level].height; volume[level].rgba.resize(mips[level].rgba.size()*asset.views.size()); }
            }
            for(unsigned level=0;level<mips.size();++level)
                std::copy(mips[level].rgba.begin(),mips[level].rgba.end(),volume[level].rgba.begin()+layer*mips[level].rgba.size());
        }
    }
    catch(const std::exception& e) { asset.error=e.what(); }
    asset.loadMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return asset;
}
GLuint texture(const Mips& mips,bool repeat)
{
    GLuint id=0; glCreateTextures(GL_TEXTURE_2D,1,&id);
    glTextureStorage2D(id,static_cast<GLsizei>(mips.size()),GL_RGBA8,mips[0].width,mips[0].height);
    for(std::size_t i=0;i<mips.size();++i) glTextureSubImage2D(id,static_cast<GLint>(i),0,0,mips[i].width,mips[i].height,GL_RGBA,GL_UNSIGNED_BYTE,mips[i].rgba.data());
    glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(id,GL_TEXTURE_WRAP_S,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE); glTextureParameteri(id,GL_TEXTURE_WRAP_T,repeat?GL_REPEAT:GL_CLAMP_TO_EDGE);
    return id;
}
GLuint textureArray(const Mips& levels)
{
    if(levels.empty()) return 0;
    GLuint id=0; glCreateTextures(GL_TEXTURE_2D_ARRAY,1,&id);
    glTextureStorage3D(id,static_cast<GLsizei>(levels.size()),GL_RGBA8,levels[0].width,levels[0].height,17);
    // All capture layers of one mip are contiguous in the worker's payload:
    // 18 uploads for both 256px arrays instead of 306 small driver calls.
    for(std::size_t level=0;level<levels.size();++level)
    {
        const auto& mip=levels[level];
        glTextureSubImage3D(id,static_cast<GLint>(level),0,0,0,mip.width,mip.height,17,GL_RGBA,GL_UNSIGNED_BYTE,mip.rgba.data());
    }
    glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(id,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTextureParameteri(id,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return id;
}
struct ModelGpu
{
    std::array<MeshGpu,2> meshes;
    GLuint bark=0,leaf=0,albedo=0,normal=0;
    std::uint64_t bytes=0,revision=0; float radius=0;
    bool ready=false,failed=false,denied=false;
    void clear()
    {
        for(auto& mesh:meshes) mesh.clear();
        const GLuint textures[]={bark,leaf,albedo,normal}; glDeleteTextures(4,textures);
        bark=leaf=albedo=normal=0; bytes=0; ready=false;
        revision=Ge::nextShadowCasterBoundsRevision();
    }
    bool upload(const Asset& asset)
    {
        UnpackState state;
        while(glGetError()!=GL_NO_ERROR) {}
        for(unsigned part=0;part<2;++part)
            if(!meshes[part].upload(asset.meshes[part])) { clear(); return false; }
        bark=texture(asset.bark,true); leaf=texture(asset.leaf,false);
        albedo=textureArray(asset.impostors[0]); normal=textureArray(asset.impostors[1]);
        if(glGetError()!=GL_NO_ERROR || !bark || !leaf) { clear(); return false; }
        bytes=asset.bytes; ready=true; revision=Ge::nextShadowCasterBoundsRevision(); return true;
    }
};
struct Program
{
    GLuint id=0;
    GLint clip=-1,view=-1,eye=-1,gamma=-1;
    GLint effectCamera=-1,effectLod=-1,wind=-1,windDirection=-1;
    void discover()
    {
        clip=glGetUniformLocation(id,"uClip"); view=glGetUniformLocation(id,"uView"); eye=glGetUniformLocation(id,"uEye");
        gamma=glGetUniformLocation(id,"uGamma");
        effectCamera=glGetUniformLocation(id,"uEffectCamera"); effectLod=glGetUniformLocation(id,"uEffectLod");
        wind=glGetUniformLocation(id,"uWind"); windDirection=glGetUniformLocation(id,"uWindDirection");
    }
};
struct Group { unsigned model=0,first=0,count=0; };
struct TransformPage
{
    ProceduralTerrain::ObjectBlock block;
    unsigned slot=0;
    bool live=false;
};
bool samePage(const ProceduralTerrain::ObjectBlock& a,const ProceduralTerrain::ObjectBlock& b)
{
    // Manual/legacy publishers with no allocation serial cannot promise that
    // the bytes of a recycled pool page remained immutable.
    return a.allocationRevision!=0 && a.allocationRevision==b.allocationRevision &&
        a.prototype==b.prototype && a.firstInstance==b.firstInstance && a.instanceCount==b.instanceCount &&
        a.sectorLattice==b.sectorLattice && a.origin==b.origin && a.extent==b.extent && a.seed==b.seed;
}
struct Pass
{
    Buffer selection,commands,visible;
    std::vector<Group> groups,scratchGroups; std::vector<std::uint32_t> blocks,scratchBlocks;
    glm::mat4 clip{0}; RenderView view;
    std::uint64_t revision=0,selectionSerial=0;
    bool selected=false,valid=false,uploadSelection=true,gpuFailed=false;
    struct Content
    {
        ProceduralTerrain::ObjectBlock block;
        std::uint64_t model=0,legacy=0;
        bool operator==(const Content& b) const noexcept
        { return block==b.block && model==b.model && legacy==b.legacy; }
    };
    std::vector<Content> content,pendingContent;
    RenderView contentView;
    std::uint64_t contentSerial=0,contentToken=0;
    bool animated=false;
    glm::vec2 contentWindPhase{0};
    void clear() { selection.clear(); commands.clear(); visible.clear(); groups.clear(); blocks.clear();
        scratchBlocks.clear(); scratchGroups.clear(); content.clear(); pendingContent.clear(); selected=valid=false; }
};
struct Candidate { unsigned index; std::array<float,3> origin; float extent; };
struct BlockBounds { std::array<float,3> origin; float extent; };
BlockBounds blockBounds(const ProceduralTerrain::ObjectBlock& b,float radius,float declaredRadius)
{
    // Positions decode relative to a stable sector. Their actual capture can
    // be a much smaller chunk: retain its bounds for CPU culling/shadow caching.
    const bool bounded=finiteFieldValue(b.boundsExtent) && b.boundsExtent>0;
    const float extra=bounded?(std::max)(0.0f,radius-declaredRadius):radius;
    auto origin=bounded?b.boundsOrigin:b.origin;
    for(auto& coordinate:origin) coordinate-=extra;
    return {origin,(bounded?b.boundsExtent:b.extent)+2*extra};
}
double distance2(const BlockBounds& b,const glm::vec3& point)
{
    double result=0;
    for(unsigned a=0;a<3;++a)
    {
        const double lo=double(b.origin[a]),hi=lo+b.extent;
        const double d=std::max(0.0,std::max(lo-point[a],point[a]-hi)); result+=d*d;
    }
    return result;
}
}

struct TreeRenderGpu::Impl
{
    RenderSettings settings; RenderStats statistics; RenderView camera;
    std::vector<ProceduralTerrain::ObjectPrototype> prototypes;
    std::vector<ProceduralTerrain::ObjectBlock> sources;
    std::vector<unsigned> sourceOrder;
    std::vector<Candidate> candidates;
    glm::vec3 candidateCamera{0};
    std::uint64_t candidateRevision=0;
    std::vector<ModelGpu> models; std::vector<ModelData> data;
    Buffer blocks,updates,instances,modelBuffer;
    std::unordered_map<unsigned,TransformPage> transformPages;
    std::vector<unsigned> freeTransformPages;
    unsigned transformPageEnd=0;
    MeshGpu quad;
    GLuint build=0,sourceBuffer=0;
    GLuint windNoise=0;
    bool windActive=false;
    glm::vec2 windPhase{0},windDirection{0};
    // Three material kinds; only impostors need a parallel-view variant.
    std::array<Program,4> opaque,shadow; // wood / foliage / perspective impostor / parallel impostor
    struct CullProgram { GLuint id=0; GLint planes=-1,cam=-1,lod=-1,projection=-1; };
    std::array<CullProgram,2> cull; // perspective / orthographic
    std::map<unsigned,Pass> passes;
    std::future<Asset> loading; unsigned loadingID=0;
    std::vector<unsigned> wanted; glm::vec3 demandCamera{0};
    std::uint64_t sourceRevision=0;
    std::uint64_t storageLimit=0;
    bool initialized=false,haveSource=false,haveDemand=false;
    std::string failure;

    void changed() { ++statistics.revision; }
    void modelTable()
    { if(!modelBuffer.allocate(data.size()*sizeof(ModelData),data.data())) failure="Cannot upload tree model table"; changed(); }
    void evict(unsigned id)
    {
        statistics.modelBytes-=models[id].bytes; --statistics.loadedModels;
        models[id].clear(); data[id].counts={}; data[id].scales.w=0;
    }
    void updateDemand(bool force)
    {
        auto delta=camera.camera-demandCamera;
        if(!force && haveDemand && glm::dot(delta,delta)<32.0f*32.0f) return;
        demandCamera=camera.camera; haveDemand=true;
        std::vector<double> nearest(models.size(),std::numeric_limits<double>::infinity());
        for(const auto& b:sources)
        {
            const float radius=prototypes[b.prototype].boundingRadius;
            nearest[b.prototype]=std::min(nearest[b.prototype],distance2(blockBounds(b,radius,radius),camera.camera));
        }
        std::vector<unsigned> order;
        for(unsigned i=0;i<models.size();++i) if(nearest[i]<double(settings.maximumDistance)*settings.maximumDistance) order.push_back(i);
        std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return nearest[a]<nearest[b] || (nearest[a]==nearest[b] && a<b);});
        if(order.size()>settings.maximumResidentModels) order.resize(settings.maximumResidentModels);
        if(order!=wanted)
        {
            wanted=std::move(order); for(auto& model:models) model.denied=false;
        }
    }
    bool makeRoom(std::uint64_t bytes,unsigned candidate)
    {
        const auto budget=std::uint64_t{settings.modelBudgetMiB}<<20u;
        if(bytes>budget) return false;
        while(statistics.modelBytes+bytes>budget || statistics.loadedModels>=settings.maximumResidentModels)
        {
            unsigned victim=static_cast<unsigned>(models.size());
            const auto rank=[&](unsigned id){return std::find(wanted.begin(),wanted.end(),id)-wanted.begin();};
            auto worst=rank(candidate);
            for(unsigned i=0;i<models.size();++i) if(models[i].ready && rank(i)>worst) { victim=i; worst=rank(i); }
            if(victim==models.size()) return false;
            evict(victim);
        }
        return true;
    }
    void assets()
    {
        if(loading.valid() && loading.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        {
            Asset asset;
            try { asset=loading.get(); }
            catch(const std::exception& e) { asset.error=e.what(); }
            auto& model=models[loadingID];
            statistics.assetLoadMilliseconds+=asset.loadMilliseconds;
            if(!asset.error.empty()) { model.failed=true; ++statistics.failedModels; failure=prototypes[loadingID].asset+": "+asset.error; }
            else if(std::find(wanted.begin(),wanted.end(),loadingID)!=wanted.end())
            {
                if(!makeRoom(asset.bytes,loadingID)) model.denied=true;
                else if([&] { const auto start=std::chrono::steady_clock::now(); const bool ok=model.upload(asset);
                    statistics.assetUploadMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); return !ok; }())
                { model.failed=true; ++statistics.failedModels; failure="Cannot upload baked tree "+prototypes[loadingID].asset; }
                else
                {
                    auto& d=data[loadingID];
                    const auto centre=(asset.tree.boundsMin+asset.tree.boundsMax)*0.5f;
                    const float radius=glm::length(asset.tree.boundsMax-centre);
                    d.centreRadius=glm::vec4(centre,radius);
                    d.crown=glm::vec4(asset.crownCentre,std::max(asset.tree.boundsMax.y,0.001f));
                    d.crownShape=glm::vec4(asset.crownInverseExtent,windActive?d.crown.w*settings.windStrength:0.0f);
                    model.radius=(glm::length(glm::max(glm::abs(asset.tree.boundsMin),glm::abs(asset.tree.boundsMax)))+d.crownShape.w)*prototypes[loadingID].maximumScale+2.0f;
                    if(!asset.views.empty())
                    { d.scales.z=asset.views[0].halfExtent; d.scales.w=1; d.impostorCentre=glm::vec4(asset.views[0].centre,0); }
                    d.counts=asset.counts; d.firstIndices=asset.firstIndices; d.baseVertices=asset.baseVertices;
                    statistics.modelBytes+=model.bytes; ++statistics.loadedModels; ++statistics.assetUploads;
                }
                modelTable(); // Also invalidates passes after an eviction/failure.
            }
        }
        if(!loading.valid()) for(unsigned i:wanted)
            if(!models[i].ready && !models[i].failed && !models[i].denied)
            {
                loadingID=i; const std::string path=prototypes[i].asset;
                try { loading=std::async(std::launch::async,[path]{return loadAsset(path);}); }
                catch(const std::exception& e) { models[i].failed=true; ++statistics.failedModels; failure=e.what(); }
                break;
            }
        statistics.pendingModels=loading.valid()?1u:0u;
        statistics.budgetSkippedModels=0;
        for(const auto& model:models) if(model.denied) ++statistics.budgetSkippedModels;
    }
    bool setSources(unsigned buffer,const std::vector<ProceduralTerrain::ObjectBlock>& input,std::uint64_t revision)
    {
        if(haveSource && sourceBuffer==buffer && sourceRevision==revision) return false;
        if(sourceBuffer!=buffer)
        { transformPages.clear(); freeTransformPages.clear(); transformPageEnd=0; }
        sourceBuffer=buffer; sourceRevision=revision; haveSource=true; sources.clear(); sourceOrder.clear(); statistics.instances=0;
        changed();
        if(input.empty() || !buffer)
        { transformPages.clear(); freeTransformPages.clear(); transformPageEnd=0; return true; }
        if(input.size()>65535) { failure="Tree block budget exceeded"; return true; }
        GLint64 sourceBytes=0;
        glGetNamedBufferParameteri64v(buffer,GL_BUFFER_SIZE,&sourceBytes);
        for(auto& entry:transformPages) entry.second.live=false;
        for(const auto& b:input)
        {
            if(b.prototype>=models.size() || !b.instanceCount || b.instanceCount>Page || !finiteFieldValue(b.extent) || b.extent<=0 ||
                !finiteFieldValue(b.origin[0]) || !finiteFieldValue(b.origin[1]) || !finiteFieldValue(b.origin[2]) ||
                (std::uint64_t{b.firstInstance}+b.instanceCount)*8>static_cast<std::uint64_t>(std::max<GLint64>(0,sourceBytes)))
            { failure="Invalid published tree block"; sources.clear(); statistics.instances=0; return true; }
            const auto found=transformPages.find(b.firstInstance);
            if(found!=transformPages.end()) found->second.live=true;
            statistics.instances+=b.instanceCount;
        }
        for(auto it=transformPages.begin();it!=transformPages.end();)
            if(!it->second.live) { freeTransformPages.push_back(it->second.slot); it=transformPages.erase(it); }
            else ++it;
        std::vector<BlockGpu> metadata,dirty;
        metadata.reserve(input.size()); dirty.reserve(input.size());
        for(const auto& b:input)
        {
            auto found=transformPages.find(b.firstInstance);
            const bool reused=found!=transformPages.end() && samePage(found->second.block,b);
            if(found==transformPages.end())
            {
                unsigned slot=transformPageEnd;
                if(freeTransformPages.empty()) ++transformPageEnd;
                else { slot=freeTransformPages.back(); freeTransformPages.pop_back(); }
                found=transformPages.emplace(b.firstInstance,TransformPage{b,slot,true}).first;
            }
            auto& cached=found->second;
            cached.block=b;
            const BlockGpu entry{glm::vec4(b.origin[0],b.origin[1],b.origin[2],b.extent),
                glm::uvec4(b.firstInstance,b.instanceCount,b.prototype,cached.slot*Page),
                glm::ivec4(b.sectorLattice[0],b.sectorLattice[1],b.sectorLattice[2],static_cast<std::int32_t>(b.seed))};
            metadata.push_back(entry);
            if(reused) ++statistics.transformPagesReused;
            else { dirty.push_back(entry); statistics.transformedInstances+=b.instanceCount; }
        }
        const std::size_t bytes=std::size_t{transformPageEnd}*Page*InstanceBytes;
        if(bytes>storageLimit || !blocks.allocate(metadata.size()*sizeof(BlockGpu),metadata.data(),false,true) ||
            !instances.allocate(bytes,nullptr,true,true) ||
            !updates.allocate(dirty.size()*sizeof(BlockGpu),dirty.data(),false,true))
        { failure="Cannot allocate tree transforms"; statistics.instances=0; transformPages.clear(); freeTransformPages.clear(); transformPageEnd=0; return true; }
        sources=input;
        sourceOrder.resize(sources.size()); std::iota(sourceOrder.begin(),sourceOrder.end(),0u);
        std::stable_sort(sourceOrder.begin(),sourceOrder.end(),[&](unsigned a,unsigned b){return sources[a].prototype<sources[b].prototype;});
        if(!dirty.empty())
        {
            // Recycled slots may still be read by an earlier draw. Order the
            // next SSBO writes after those reads, not only before the next draw.
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,buffer); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,instances.id);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,updates.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,modelBuffer.id);
            glUseProgram(build); glDispatchCompute(static_cast<GLuint>(dirty.size()),1,1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); ++statistics.transformBuilds;
            statistics.transformPagesBuilt+=dirty.size();
        }
        return true;
    }
    void updateCandidates()
    {
        if(candidateRevision==statistics.revision && candidateCamera==camera.camera) return;
        candidates.clear(); candidates.reserve(sources.size());
        for(unsigned i:sourceOrder)
        {
            const auto& b=sources[i]; const auto& model=models[b.prototype];
            if(!model.ready) continue;
            const auto bounds=blockBounds(b,model.radius,prototypes[b.prototype].boundingRadius);
            if(distance2(bounds,camera.camera)>double(settings.maximumDistance)*settings.maximumDistance) continue;
            candidates.push_back({i,bounds.origin,bounds.extent});
        }
        candidateRevision=statistics.revision; candidateCamera=camera.camera;
        ++statistics.distanceSelections;
    }
    // CPU-only: used both before shadow-cache decisions and before drawing.
    // Empty/out-of-range passes do not capture GL state or touch the driver.
    void select(Pass& pass,const glm::mat4& clip)
    {
        if(pass.selected && pass.revision==statistics.revision && pass.clip==clip && pass.view.camera==camera.camera &&
            pass.view.projectionScale==camera.projectionScale && pass.view.orthographic==camera.orthographic) return;
        updateCandidates();
        pass.valid=false; pass.scratchBlocks.clear();
        const auto frustum=ProceduralTerrain::makeFrustum(clip);
        for(const auto& c:candidates)
            if(ProceduralTerrain::intersectsFrustum(frustum,c.origin,c.extent)) pass.scratchBlocks.push_back(c.index);
        if(pass.blocks!=pass.scratchBlocks)
        {
            pass.blocks.swap(pass.scratchBlocks); pass.groups.clear(); pass.uploadSelection=true;
            for(unsigned i=0;i<pass.blocks.size();++i)
            {
                unsigned type=sources[pass.blocks[i]].prototype;
                if(pass.groups.empty() || pass.groups.back().model!=type) pass.groups.push_back({type,i,0});
                ++pass.groups.back().count;
            }
        }
        // Equal block indices can point at different models after a publication.
        // Rebuild grouping in that case; comparisons keep the upload exact.
        else if(pass.revision!=statistics.revision && !pass.blocks.empty())
        {
            auto& groups=pass.scratchGroups; groups.clear();
            for(unsigned i=0;i<pass.blocks.size();++i)
            {
                unsigned type=sources[pass.blocks[i]].prototype;
                if(groups.empty() || groups.back().model!=type) groups.push_back({type,i,0});
                ++groups.back().count;
            }
            bool equal=groups.size()==pass.groups.size();
            for(unsigned i=0;equal && i<groups.size();++i) equal=groups[i].model==pass.groups[i].model &&
                groups[i].first==pass.groups[i].first && groups[i].count==pass.groups[i].count;
            if(!equal) { pass.groups.swap(groups); pass.uploadSelection=true; }
        }
        pass.clip=clip; pass.view=camera; pass.revision=statistics.revision; pass.selected=true;
        pass.animated=false;
        if(windActive) for(unsigned index:pass.blocks)
        {
            const auto& b=sources[index];
            const auto& model=data[b.prototype];
            const double nearest=std::sqrt(distance2(blockBounds(b,models[b.prototype].radius,prototypes[b.prototype].boundingRadius),camera.camera));
            const double projected=model.centreRadius.w*double(model.scales.y)*camera.projectionScale/
                (camera.orthographic?1.0:std::max(nearest,0.01));
            if(nearest<settings.windDistance && (model.scales.w<0.5f || projected>settings.impostorCoverage*(1+settings.transitionFraction)))
            { pass.animated=true; break; }
        }
        ++pass.selectionSerial;
        if(pass.blocks.empty()) { pass.valid=true; pass.gpuFailed=false; }
    }
    std::uint64_t shadowContent(Pass& pass,const glm::mat4& clip)
    {
        select(pass,clip);
        // A failed draw must not leave an empty shadow cached under a valid
        // content token. Zero requests a conservative retry on the next frame.
        if(pass.gpuFailed) return 0;
        const bool animationChanged=pass.animated && pass.contentWindPhase!=windPhase;
        if(pass.contentToken && pass.contentSerial==pass.selectionSerial)
        {
            if(animationChanged) pass.contentToken=Ge::nextShadowCasterBoundsRevision();
            pass.contentWindPhase=windPhase;
            return pass.contentToken;
        }
        pass.pendingContent.clear(); pass.pendingContent.reserve(pass.blocks.size());
        for(unsigned i:pass.blocks)
        {
            const auto& b=sources[i];
            pass.pendingContent.push_back({b,models[b.prototype].revision,b.allocationRevision?0:sourceRevision});
        }
        const bool lodChanged=!pass.blocks.empty() && (pass.contentView.camera!=camera.camera ||
            pass.contentView.projectionScale!=camera.projectionScale || pass.contentView.orthographic!=camera.orthographic);
        if(!pass.contentToken || lodChanged || animationChanged || pass.content!=pass.pendingContent)
        {
            pass.content.swap(pass.pendingContent);
            pass.contentToken=Ge::nextShadowCasterBoundsRevision();
        }
        pass.contentView=camera; pass.contentSerial=pass.selectionSerial;
        pass.contentWindPhase=windPhase;
        return pass.contentToken;
    }
    void cache(Pass& pass)
    {
        if(pass.valid) return;
        const auto n=pass.blocks.size();
        if(n)
        {
            std::vector<glm::uvec4> selection;
            if(pass.uploadSelection)
            {
                selection.reserve(n);
                for(unsigned i=0;i<pass.groups.size();++i)
                    for(unsigned j=0;j<pass.groups[i].count;++j)
                        selection.emplace_back(pass.blocks[pass.groups[i].first+j],i,pass.groups[i].first,pass.groups[i].count);
            }
            if(!pass.selection.allocate(n*sizeof(glm::uvec4),pass.uploadSelection?selection.data():nullptr,false,true) ||
                !pass.commands.allocate(pass.groups.size()*5*CommandBytes,nullptr,false,true) ||
                !pass.visible.allocate(n*3*Page*VisibleBytes,nullptr,false,true))
            {
                failure="Cannot allocate tree visibility buffers"; pass.gpuFailed=true;
                // Retain the CPU selection: next frame retries once, without
                // spinning and without waiting for an unrelated publication.
                return;
            }
            if(pass.uploadSelection) { ++statistics.selectionUploads; pass.uploadSelection=false; }
            const GLuint zero=0;
            // Clear follows earlier shader writes to commands; culling also
            // reuses visible references that previous draws may still read.
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
            glClearNamedBufferData(pass.commands.id,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,pass.selection.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,pass.commands.id);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,instances.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,blocks.id);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,modelBuffer.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,pass.visible.id);
            std::array<glm::vec4,6> planesData;
            const auto frustum=ProceduralTerrain::makeFrustum(pass.clip);
            for(unsigned i=0;i<6;++i)
            {
                const double length=glm::length(glm::dvec3(frustum.planes[i]));
                planesData[i]=length>0?glm::vec4(frustum.planes[i]/length):glm::vec4(0,0,0,1);
            }
            const auto& program=cull[camera.orthographic?1:0];
            glUseProgram(program.id); glUniform4fv(program.planes,6,glm::value_ptr(planesData[0])); glUniform3fv(program.cam,1,glm::value_ptr(camera.camera));
            glUniform4f(program.lod,settings.lod0Coverage,settings.impostorCoverage,settings.transitionFraction,settings.maximumDistance);
            glUniform1f(program.projection,camera.projectionScale);
            glDispatchCompute(static_cast<GLuint>(n),1,1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT); ++statistics.cullDispatches;
        }
        pass.valid=true; pass.gpuFailed=false;
        if(failure=="Cannot allocate tree visibility buffers") failure.clear();
    }
    void memory()
    {
        statistics.workingBytes=blocks.capacity+updates.capacity+instances.capacity+modelBuffer.capacity+(windNoise?WindNoiseSize*WindNoiseSize:0);
        for(const auto& entry:passes) statistics.workingBytes+=entry.second.selection.capacity+entry.second.commands.capacity+entry.second.visible.capacity;
    }
};

TreeRenderGpu::TreeRenderGpu():m(std::make_unique<Impl>()) {}
TreeRenderGpu::~TreeRenderGpu() { shutdown(); }
bool TreeRenderGpu::initialize(const std::vector<ProceduralTerrain::ObjectPrototype>& prototypes,const RenderSettings& settings)
{
    shutdown(); auto& s=*m; s.failure.clear(); s.settings=settings; s.prototypes=prototypes;
    const float values[]={settings.lod0Coverage,settings.impostorCoverage,settings.transitionFraction,settings.maximumDistance,
        settings.foliageNormalBlend,settings.windStrength,settings.windScale,settings.windDistance,settings.windSpeed.x,settings.windSpeed.y};
    for(float v:values) if(!finiteFieldValue(v)) { s.failure="Non-finite tree rendering setting"; return false; }
    if(prototypes.empty() || prototypes.size()>255 || settings.maximumDistance<=0 || settings.impostorCoverage<=0 ||
        settings.lod0Coverage*(1-settings.transitionFraction)<=settings.impostorCoverage*(1+settings.transitionFraction) ||
        settings.transitionFraction<0 || settings.transitionFraction>0.4f || !settings.modelBudgetMiB || settings.modelBudgetMiB>4096 ||
        !settings.maximumResidentModels || settings.maximumResidentModels>255 ||
        settings.foliageNormalBlend<0 || settings.foliageNormalBlend>1 || settings.windStrength<0 || settings.windStrength>0.1f ||
        settings.windScale<0.1f || settings.windScale>10000 || settings.windDistance<=0 || settings.windDistance>100000 ||
        std::abs(settings.windSpeed.x)>10000 || std::abs(settings.windSpeed.y)>10000)
    { s.failure="Invalid tree render settings"; return false; }
    for(const auto& prototype:prototypes)
        if(prototype.asset.empty() || !finiteFieldValue(prototype.minimumScale) || !finiteFieldValue(prototype.maximumScale) ||
            !finiteFieldValue(prototype.boundingRadius) || prototype.minimumScale<=0 ||
            prototype.maximumScale<prototype.minimumScale || prototype.boundingRadius<0)
        { s.failure="Invalid tree model settings"; return false; }
    State state;
    s.windActive=settings.windEnabled && settings.windStrength>0 && glm::dot(settings.windSpeed,settings.windSpeed)>0;
    s.windDirection=s.windActive?glm::normalize(settings.windSpeed):glm::vec2(0);
    GLint64 storageLimit=0; glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&storageLimit);
    s.storageLimit=static_cast<std::uint64_t>(std::max<GLint64>(0,storageLimit));
    const auto path=[&](const char* name){return settings.shaderDirectory+"/"+name;};
    s.build=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,path("instances.C.glsl"),{}}},s.failure,"Tree transforms",{true,{}});
    if(!s.build) { shutdown(); return false; }
    for(unsigned orthographic=0;orthographic<2;++orthographic)
    {
        auto& program=s.cull[orthographic];
        const std::string define=std::string(orthographic?"#define TREE_ORTHOGRAPHIC 1\n":"#define TREE_ORTHOGRAPHIC 0\n")+
            (settings.transitionFraction>0?"#define TREE_LOD_TRANSITIONS 1\n":"#define TREE_LOD_TRANSITIONS 0\n");
        program.id=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,path("cull.C.glsl"),define}},s.failure,"Tree LOD and visibility",{true,{}});
        if(!program.id) { shutdown(); return false; }
        program.planes=glGetUniformLocation(program.id,"uPlanes"); program.cam=glGetUniformLocation(program.id,"uCamera");
        program.lod=glGetUniformLocation(program.id,"uLod"); program.projection=glGetUniformLocation(program.id,"uProjectionScale");
    }
    for(unsigned i=0;i<2;++i) for(unsigned kind=0;kind<4;++kind)
    {
        auto& p=i?s.shadow[kind]:s.opaque[kind];
        const std::string define=std::string(i?"#define TREE_SHADOW 1\n":"#define TREE_SHADOW 0\n")+
            (kind>=2?"#define TREE_IMPOSTOR 1\n":"#define TREE_IMPOSTOR 0\n")+
            (kind==1?"#define TREE_FOLIAGE 1\n":"#define TREE_FOLIAGE 0\n")+
            (kind==3?"#define TREE_PARALLEL 1\n":"#define TREE_PARALLEL 0\n")+
            (kind<2 && s.windActive?"#define TREE_WIND 1\n":"#define TREE_WIND 0\n")+
            (kind==1 && !i && settings.foliageNormalBlend>0?"#define TREE_CROWN_NORMALS 1\n":"#define TREE_CROWN_NORMALS 0\n");
        p.id=Ge::buildShaderProgram({{GL_VERTEX_SHADER,path("render.vs.glsl"),define},{GL_FRAGMENT_SHADER,path("render.fs.glsl"),define}},s.failure,i?"Tree shadows":"Tree G-buffer",{true,{}});
        if(!p.id) { shutdown(); return false; }
        p.discover();
    }
    s.models.resize(prototypes.size()); s.data.resize(prototypes.size());
    for(unsigned i=0;i<prototypes.size();++i) s.data[i].scales=glm::vec4(prototypes[i].minimumScale,prototypes[i].maximumScale,1,0);
    s.modelTable();
    Mesh quad; quad.vertices.resize(4);
    quad.vertices[0].position={-1,-1,0}; quad.vertices[1].position={1,-1,0}; quad.vertices[2].position={1,1,0}; quad.vertices[3].position={-1,1,0};
    quad.indices={0,1,2,0,2,3};
    if(!s.quad.upload(packMesh(quad)) || s.modelBuffer.capacity!=s.data.size()*sizeof(ModelData)) { s.failure="Cannot initialize tree renderer buffers"; shutdown(); return false; }
    if(s.windActive)
    {
        // One cached, periodic value-noise lattice (16 KiB). Nearby vertices of
        // an instance sample the same texel; no procedural hash per vertex.
        std::array<std::uint8_t,WindNoiseSize*WindNoiseSize> noise{};
        for(unsigned i=0;i<noise.size();++i)
        { unsigned x=i+0x724ba349u; x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; noise[i]=static_cast<std::uint8_t>((x^(x>>16))>>24); }
        UnpackState unpack;
        glCreateTextures(GL_TEXTURE_2D,1,&s.windNoise); glTextureStorage2D(s.windNoise,1,GL_R8,WindNoiseSize,WindNoiseSize);
        glTextureSubImage2D(s.windNoise,0,0,0,WindNoiseSize,WindNoiseSize,GL_RED,GL_UNSIGNED_BYTE,noise.data());
        glTextureParameteri(s.windNoise,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTextureParameteri(s.windNoise,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTextureParameteri(s.windNoise,GL_TEXTURE_WRAP_S,GL_REPEAT); glTextureParameteri(s.windNoise,GL_TEXTURE_WRAP_T,GL_REPEAT);
        GLint width=0; glGetTextureLevelParameteriv(s.windNoise,0,GL_TEXTURE_WIDTH,&width);
        if(width!=WindNoiseSize) { s.failure="Cannot initialize tree wind noise"; shutdown(); return false; }
    }
    s.initialized=true; return true;
}
void TreeRenderGpu::prepare(unsigned buffer,const std::vector<ProceduralTerrain::ObjectBlock>& blocks,std::uint64_t revision,const RenderView& view)
{
    auto& s=*m; if(!s.initialized) return;
    s.camera=view;
    // Wrap in double precision before uploading, including for long sessions.
    if(s.windActive && finiteFieldValue(static_cast<float>(view.timeSeconds))) for(unsigned axis=0;axis<2;++axis)
        s.windPhase[axis]=static_cast<float>(std::fmod(view.timeSeconds*(double(s.settings.windSpeed[axis])/(WindNoiseSize*s.settings.windScale)),1.0));
    bool changed=!s.haveSource || s.sourceBuffer!=buffer || s.sourceRevision!=revision;
    // A publisher revision is a notification, not necessarily changed content.
    // Exact immutable-page identity permits reusing the complete command cache.
    // Legacy publishers without allocation identities remain conservative.
    if(changed && s.haveSource && s.sourceBuffer==buffer && blocks==s.sources &&
        std::all_of(blocks.begin(),blocks.end(),[](const auto& b){return b.allocationRevision!=0;}))
    { s.sourceRevision=revision; changed=false; }
    if(changed)
    {
        State state;
        s.setSources(buffer,blocks,revision);
    }
    // Asset uploads use DSA and their own unpack guard. An unchanged prepare
    // never queries/restores GL bindings or dispatches a transform rebuild.
    s.updateDemand(changed); s.assets(); s.memory(); s.statistics.shadowBlocks=0;
    if(s.sources.empty()) s.statistics.cameraBlocks=0;
}
void TreeRenderGpu::draw(std::uint32_t passID,const glm::mat4& clip)
{
    auto& s=*m; if(!s.initialized || s.sources.empty()) return;
    auto& pass=s.passes[passID]; s.select(pass,clip);
    if(passID==0) s.statistics.cameraBlocks=static_cast<unsigned>(pass.blocks.size());
    else s.statistics.shadowBlocks+=static_cast<unsigned>(pass.blocks.size());
    if(pass.blocks.empty()) return;
    State state(true,!pass.valid,passID==0,s.windActive); s.cache(pass);
    if(!pass.valid) return;
    bool parallel=false; glm::vec3 eye=s.camera.camera;
    if(passID || s.camera.orthographic)
    {
        const auto inverse=glm::inverse(clip);
        const glm::vec4 point=inverse*glm::vec4(0,0,1,0);
        parallel=std::abs(point.w)<0.000001f;
        if(parallel)
        {
            auto nearPoint=inverse*glm::vec4(0,0,-1,1),farPoint=inverse*glm::vec4(0,0,1,1);
            eye=glm::normalize(glm::vec3(nearPoint)/nearPoint.w-glm::vec3(farPoint)/farPoint.w);
        }
        else eye=glm::vec3(point)/point.w;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,s.instances.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,s.modelBuffer.id);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,pass.visible.id); glBindBuffer(GL_DRAW_INDIRECT_BUFFER,pass.commands.id);
    for(unsigned i=0;i<(passID?2u:3u);++i) glBindSampler(i,0);
    if(s.windActive) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,s.windNoise); glBindSampler(3,0); }
    GLuint program=0;
    for(unsigned kind=0;kind<3;++kind)
    {
        const bool billboard=kind==2,foliage=kind==1;
        auto& p=(passID?s.shadow:s.opaque)[billboard && parallel?3:kind];
        if(program!=p.id)
        {
            glUseProgram(p.id); program=p.id;
            glUniformMatrix4fv(p.clip,1,GL_FALSE,glm::value_ptr(clip));
            if(p.view>=0) glUniformMatrix4fv(p.view,1,GL_FALSE,glm::value_ptr(s.camera.view));
            if(p.gamma>=0) glUniform1f(p.gamma,s.camera.albedoGamma);
            if(p.eye>=0) glUniform3fv(p.eye,1,glm::value_ptr(eye));
            if(p.effectCamera>=0) glUniform4f(p.effectCamera,s.camera.camera.x,s.camera.camera.y,s.camera.camera.z,s.camera.projectionScale);
            if(p.effectLod>=0) glUniform4f(p.effectLod,s.settings.impostorCoverage*(1+s.settings.transitionFraction),
                s.camera.orthographic?1.0f:0.0f,s.settings.windDistance,s.settings.foliageNormalBlend);
            if(p.wind>=0) glUniform4f(p.wind,s.windPhase.x,s.windPhase.y,1.0f/(WindNoiseSize*s.settings.windScale),0);
            if(p.windDirection>=0) glUniform2fv(p.windDirection,1,glm::value_ptr(s.windDirection));
        }
        if(billboard || foliage) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(passID?GL_FRONT:GL_BACK); }
        for(unsigned groupID=0;groupID<pass.groups.size();++groupID)
        {
            const auto& model=s.models[pass.groups[groupID].model];
            if(billboard?!model.albedo:!model.meshes[kind].count) continue;
            if(billboard)
            {
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY,model.albedo);
                if(!passID) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D_ARRAY,model.normal); }
                glBindVertexArray(s.quad.vao);
            }
            else { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,foliage?model.leaf:model.bark); glBindVertexArray(model.meshes[kind].vao); }
            const auto offset=static_cast<std::uintptr_t>((std::size_t{groupID}*5+(billboard?4:kind))*CommandBytes);
            const unsigned commands=billboard?1:2;
            glMultiDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_INT,reinterpret_cast<const void*>(offset),commands,billboard?0:2*CommandBytes);
            ++s.statistics.drawCalls; s.statistics.indirectCommands+=commands;
        }
    }
    s.memory();
}
void TreeRenderGpu::appendBounds(std::vector<Ge::ShadowCasterBounds>& output) const
{
    for(const auto& b:m->sources) if(m->models[b.prototype].ready)
    {
        const auto bounds=blockBounds(b,m->models[b.prototype].radius,m->prototypes[b.prototype].boundingRadius);
        const glm::dvec3 lo(bounds.origin[0],bounds.origin[1],bounds.origin[2]);
        output.push_back({lo,lo+glm::dvec3(bounds.extent)});
    }
}
std::uint64_t TreeRenderGpu::shadowContentRevision(std::uint32_t passID,const glm::mat4& clip) const
{
    auto& s=*m;
    if(!s.initialized) return 0;
    return s.shadowContent(s.passes[passID],clip);
}
void TreeRenderGpu::shutdown() noexcept
{
    if(!m) return; auto& s=*m;
    if(s.loading.valid()) { try { s.loading.wait(); s.loading.get(); } catch(...) {} }
    for(auto& item:s.passes) item.second.clear(); s.passes.clear();
    for(auto& model:s.models) model.clear(); s.models.clear(); s.data.clear();
    s.blocks.clear(); s.updates.clear(); s.instances.clear(); s.modelBuffer.clear(); s.quad.clear();
    for(auto* programs:{&s.opaque,&s.shadow}) for(auto& p:*programs) { if(p.id) glDeleteProgram(p.id); p.id=0; }
    if(s.build) glDeleteProgram(s.build);
    if(s.windNoise) glDeleteTextures(1,&s.windNoise);
    s.windNoise=0; s.windActive=false; s.windPhase={};
    for(auto& p:s.cull) { if(p.id) glDeleteProgram(p.id); p={}; }
    s.build=0; s.sources.clear(); s.sourceOrder.clear(); s.candidates.clear(); s.candidateRevision=0; s.wanted.clear();
    s.transformPages.clear(); s.freeTransformPages.clear(); s.transformPageEnd=0;
    s.statistics={}; s.initialized=s.haveSource=s.haveDemand=false; s.sourceBuffer=0;
}
const RenderStats& TreeRenderGpu::stats() const noexcept { return m->statistics; }
const std::string& TreeRenderGpu::error() const noexcept { return m->failure; }
}
