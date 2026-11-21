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

namespace EnduranceTrees
{
namespace
{
using ProceduralTerrain::finiteFieldValue;
constexpr unsigned Page=ProceduralTerrain::ObjectPageSize;
static_assert(Page==64,"Tree culling workgroups match object pages");
struct BlockGpu { glm::vec4 origin; glm::uvec4 info; glm::ivec4 anchor; };
struct ModelData { glm::vec4 centreRadius,scales,impostorCentre; glm::uvec4 counts; };
static_assert(sizeof(BlockGpu)==48 && sizeof(ModelData)==64,"std430 tree layout");
constexpr unsigned InstanceBytes=32,CommandBytes=20,VisibleBytes=8;

struct State
{
    GLint program=0,vao=0,storage=0,indirect=0,active=0,cullMode=0;
    bool culling=false;
    static constexpr std::array<GLuint,6> indices{{1,2,4,5,6,7}};
    std::array<GLint,6> buffers{}; std::array<GLint64,6> starts{},sizes{};
    std::array<GLint,3> textures{},arrays{},samplers{};
    State()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage); glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING,&indirect);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);
        culling=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
        for(unsigned i=0;i<indices.size();++i)
        {
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,indices[i],&buffers[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,indices[i],&starts[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,indices[i],&sizes[i]);
        }
        for(unsigned i=0;i<3;++i)
        {
            glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&arrays[i]); glGetIntegeri_v(GL_SAMPLER_BINDING,i,&samplers[i]);
        }
    }
    ~State()
    {
        for(unsigned i=0;i<indices.size();++i)
            if(buffers[i] && sizes[i]>0) glBindBufferRange(GL_SHADER_STORAGE_BUFFER,indices[i],buffers[i],starts[i],sizes[i]);
            else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,indices[i],buffers[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,storage); glBindBuffer(GL_DRAW_INDIRECT_BUFFER,indirect);
        for(unsigned i=0;i<3;++i)
        {
            glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,textures[i]);
            glBindTexture(GL_TEXTURE_2D_ARRAY,arrays[i]); glBindSampler(i,samplers[i]);
        }
        glActiveTexture(active); glUseProgram(program); glBindVertexArray(vao);
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
    bool allocate(std::size_t bytes,const void* data=nullptr)
    {
        if(!bytes) return true;
        if(!id) glCreateBuffers(1,&id);
        if(bytes>capacity)
        {
            glNamedBufferData(id,static_cast<GLsizeiptr>(bytes),data,GL_DYNAMIC_DRAW);
            GLint64 actual=0; glGetNamedBufferParameteri64v(id,GL_BUFFER_SIZE,&actual);
            if(actual!=static_cast<GLint64>(bytes)) return false;
            capacity=bytes;
        }
        else if(data) glNamedBufferSubData(id,0,static_cast<GLsizeiptr>(bytes),data);
        return id!=0;
    }
    void clear() { if(id) glDeleteBuffers(1,&id); id=0; capacity=0; }
};
struct MeshGpu
{
    GLuint vao=0; Buffer vertices,indices; std::uint32_t count=0;
    bool upload(const Mesh& mesh)
    {
        if(mesh.indices.empty()) return true;
        if(mesh.indices.size()>static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)())) return false;
        glCreateVertexArrays(1,&vao);
        if(!vao || !vertices.allocate(mesh.vertices.size()*sizeof(Vertex),mesh.vertices.data()) ||
            !indices.allocate(mesh.indices.size()*4,mesh.indices.data())) return false;
        glVertexArrayVertexBuffer(vao,0,vertices.id,0,sizeof(Vertex)); glVertexArrayElementBuffer(vao,indices.id);
        const GLuint offsets[]={offsetof(Vertex,position),offsetof(Vertex,normal),offsetof(Vertex,uv),offsetof(Vertex,color)};
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
    Mips bark,leaf; std::array<std::vector<Mips>,2> impostors;
    std::uint64_t bytes=0; std::string error;
};
std::uint8_t byte(float value) { return static_cast<std::uint8_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f); }
float linear(float v) { return v<=0.04045f?v/12.92f:std::pow((v+0.055f)/1.055f,2.4f); }
Asset loadAsset(const std::string& path)
{
    Asset asset;
    try
    {
        if(!loadBakedTree(path,asset.tree,asset.error,&asset.views)) return asset;
        asset.tree.branches.clear();
        asset.bark=coverageMipmaps(asset.tree.barkTexture); asset.leaf=coverageMipmaps(asset.tree.leafMask);
        const auto addBytes=[&](const Mips& mips){ for(const auto& mip:mips) asset.bytes+=mip.rgba.size(); };
        addBytes(asset.bark); addBytes(asset.leaf);
        for(const auto& lod:asset.tree.lods) for(const auto* mesh:{&lod.wood,&lod.foliage})
            asset.bytes+=mesh->vertices.size()*sizeof(Vertex)+mesh->indices.size()*4;
        for(unsigned channel=0;channel<2;++channel) for(const auto& view:asset.views)
        {
            Image source=channel==0?view.albedo:view.normal;
            if(channel==0) for(std::size_t i=0;i<source.rgba.size();i+=4) for(unsigned c=0;c<3;++c)
                source.rgba[i+c]=byte(linear(source.rgba[i+c]/255.0f));
            auto mips=coverageMipmaps(source);
            for(auto& mip:mips)
            {
                for(std::size_t i=0;i<mip.rgba.size();i+=4) for(unsigned c=0;c<3;++c)
                    mip.rgba[i+c]=byte(mip.rgba[i+c]/255.0f*mip.rgba[i+3]/255.0f);
                const auto row=static_cast<std::size_t>(mip.width)*4;
                for(int y=0;y<mip.height/2;++y) std::swap_ranges(mip.rgba.begin()+y*row,mip.rgba.begin()+(y+1)*row,
                    mip.rgba.begin()+(mip.height-1-y)*row);
            }
            addBytes(mips); asset.impostors[channel].push_back(std::move(mips));
        }
    }
    catch(const std::exception& e) { asset.error=e.what(); }
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
GLuint textureArray(const std::vector<Mips>& layers)
{
    if(layers.empty()) return 0;
    GLuint id=0; glCreateTextures(GL_TEXTURE_2D_ARRAY,1,&id);
    glTextureStorage3D(id,static_cast<GLsizei>(layers[0].size()),GL_RGBA8,layers[0][0].width,layers[0][0].height,17);
    for(unsigned layer=0;layer<17;++layer) for(std::size_t level=0;level<layers[layer].size();++level)
    {
        const auto& mip=layers[layer][level];
        glTextureSubImage3D(id,static_cast<GLint>(level),0,0,layer,mip.width,mip.height,1,GL_RGBA,GL_UNSIGNED_BYTE,mip.rgba.data());
    }
    glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(id,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTextureParameteri(id,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return id;
}
struct ModelGpu
{
    std::array<MeshGpu,4> meshes;
    GLuint bark=0,leaf=0,albedo=0,normal=0;
    std::uint64_t bytes=0; float radius=0;
    bool ready=false,failed=false,denied=false;
    void clear()
    {
        for(auto& mesh:meshes) mesh.clear();
        const GLuint textures[]={bark,leaf,albedo,normal}; glDeleteTextures(4,textures);
        bark=leaf=albedo=normal=0; bytes=0; ready=false;
    }
    bool upload(const Asset& asset)
    {
        UnpackState state;
        while(glGetError()!=GL_NO_ERROR) {}
        for(unsigned lod=0;lod<2;++lod)
            if(!meshes[lod*2].upload(asset.tree.lods[lod].wood) || !meshes[lod*2+1].upload(asset.tree.lods[lod].foliage)) { clear(); return false; }
        bark=texture(asset.bark,true); leaf=texture(asset.leaf,false);
        albedo=textureArray(asset.impostors[0]); normal=textureArray(asset.impostors[1]);
        if(glGetError()!=GL_NO_ERROR || !bark || !leaf) { clear(); return false; }
        bytes=asset.bytes; ready=true; return true;
    }
};
struct Program
{
    GLuint id=0;
    GLint clip=-1,view=-1,eye=-1,parallel=-1,kind=-1,gamma=-1;
    void discover()
    {
        clip=glGetUniformLocation(id,"uClip"); view=glGetUniformLocation(id,"uView"); eye=glGetUniformLocation(id,"uEye");
        parallel=glGetUniformLocation(id,"uParallel"); kind=glGetUniformLocation(id,"uKind"); gamma=glGetUniformLocation(id,"uGamma");
    }
};
struct Group { unsigned model=0,first=0,count=0; };
struct Pass
{
    Buffer selection,commands,visible;
    std::vector<Group> groups; std::vector<std::uint32_t> blocks;
    glm::mat4 clip{0}; RenderView view;
    std::uint64_t revision=0; bool valid=false;
    void clear() { selection.clear(); commands.clear(); visible.clear(); groups.clear(); blocks.clear(); valid=false; }
};
double distance2(const ProceduralTerrain::ObjectBlock& b,float radius,const glm::vec3& point)
{
    double result=0;
    for(unsigned a=0;a<3;++a)
    {
        const double lo=double(b.origin[a])-radius,hi=double(b.origin[a])+b.extent+radius;
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
    std::vector<ModelGpu> models; std::vector<ModelData> data;
    Buffer blocks,instances,modelBuffer;
    MeshGpu quad;
    GLuint build=0,cull=0,sourceBuffer=0;
    Program opaque,shadow;
    GLint planes=-1,cam=-1,lod=-1,projection=-1,orthographic=-1;
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
        for(const auto& b:sources) nearest[b.prototype]=std::min(nearest[b.prototype],distance2(b,prototypes[b.prototype].boundingRadius,camera.camera));
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
            if(!asset.error.empty()) { model.failed=true; ++statistics.failedModels; failure=prototypes[loadingID].asset+": "+asset.error; }
            else if(std::find(wanted.begin(),wanted.end(),loadingID)!=wanted.end())
            {
                if(!makeRoom(asset.bytes,loadingID)) model.denied=true;
                else if(!model.upload(asset)) { model.failed=true; ++statistics.failedModels; failure="Cannot upload baked tree "+prototypes[loadingID].asset; }
                else
                {
                    auto& d=data[loadingID];
                    const auto centre=(asset.tree.boundsMin+asset.tree.boundsMax)*0.5f;
                    const float radius=glm::length(asset.tree.boundsMax-centre);
                    d.centreRadius=glm::vec4(centre,radius);
                    model.radius=glm::length(glm::max(glm::abs(asset.tree.boundsMin),glm::abs(asset.tree.boundsMax)))*prototypes[loadingID].maximumScale+2.0f;
                    if(!asset.views.empty())
                    { d.scales.z=asset.views[0].halfExtent; d.scales.w=1; d.impostorCentre=glm::vec4(asset.views[0].centre,0); }
                    for(unsigned i=0;i<4;++i) d.counts[i]=model.meshes[i].count;
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
        sourceBuffer=buffer; sourceRevision=revision; haveSource=true; sources.clear(); statistics.instances=0;
        changed();
        if(input.empty() || !buffer) return true;
        if(input.size()>65535) { failure="Tree block budget exceeded"; return true; }
        GLint64 sourceBytes=0;
        glGetNamedBufferParameteri64v(buffer,GL_BUFFER_SIZE,&sourceBytes);
        std::vector<BlockGpu> metadata; metadata.reserve(input.size());
        for(const auto& b:input)
        {
            if(b.prototype>=models.size() || !b.instanceCount || b.instanceCount>Page || !finiteFieldValue(b.extent) || b.extent<=0 ||
                !finiteFieldValue(b.origin[0]) || !finiteFieldValue(b.origin[1]) || !finiteFieldValue(b.origin[2]) ||
                (std::uint64_t{b.firstInstance}+b.instanceCount)*8>static_cast<std::uint64_t>(std::max<GLint64>(0,sourceBytes)))
            { failure="Invalid published tree block"; sources.clear(); statistics.instances=0; return true; }
            metadata.push_back({glm::vec4(b.origin[0],b.origin[1],b.origin[2],b.extent),
                glm::uvec4(b.firstInstance,b.instanceCount,b.prototype,statistics.instances),
                glm::ivec4(b.sectorLattice[0],b.sectorLattice[1],b.sectorLattice[2],static_cast<std::int32_t>(b.seed))});
            statistics.instances+=b.instanceCount;
        }
        if(std::uint64_t{statistics.instances}*InstanceBytes>storageLimit ||
            !blocks.allocate(metadata.size()*sizeof(BlockGpu),metadata.data()) || !instances.allocate(std::size_t{statistics.instances}*InstanceBytes))
        { failure="Cannot allocate tree transforms"; statistics.instances=0; return true; }
        sources=input;
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,buffer); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,instances.id);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,blocks.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,modelBuffer.id);
        glUseProgram(build); glDispatchCompute(static_cast<GLuint>(sources.size()),1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); ++statistics.transformBuilds;
        return true;
    }
    void cache(Pass& pass,const glm::mat4& clip)
    {
        if(pass.valid && pass.revision==statistics.revision && pass.clip==clip && pass.view.camera==camera.camera &&
            pass.view.projectionScale==camera.projectionScale && pass.view.orthographic==camera.orthographic) return;
        pass.valid=false; pass.blocks.clear(); pass.groups.clear();
        auto frustum=ProceduralTerrain::makeFrustum(clip);
        for(unsigned i=0;i<sources.size();++i)
        {
            const auto& b=sources[i]; const auto& model=models[b.prototype];
            if(!model.ready || distance2(b,model.radius,camera.camera)>double(settings.maximumDistance)*settings.maximumDistance) continue;
            std::array<float,3> origin{{b.origin[0]-model.radius,b.origin[1]-model.radius,b.origin[2]-model.radius}};
            if(!ProceduralTerrain::intersectsFrustum(frustum,origin,b.extent+2*model.radius)) continue;
            pass.blocks.push_back(i);
        }
        std::stable_sort(pass.blocks.begin(),pass.blocks.end(),[&](unsigned a,unsigned b){return sources[a].prototype<sources[b].prototype;});
        for(unsigned i=0;i<pass.blocks.size();++i)
        {
            unsigned type=sources[pass.blocks[i]].prototype;
            if(pass.groups.empty() || pass.groups.back().model!=type) pass.groups.push_back({type,i,0});
            ++pass.groups.back().count;
        }
        const auto n=pass.blocks.size();
        if(n)
        {
            if(!pass.selection.allocate(n*4,pass.blocks.data()) || !pass.commands.allocate(n*5*CommandBytes) || !pass.visible.allocate(n*3*Page*VisibleBytes))
            { failure="Cannot allocate tree visibility buffers"; pass.blocks.clear(); pass.groups.clear(); return; }
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,pass.selection.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,pass.commands.id);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,instances.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,blocks.id);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,modelBuffer.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,pass.visible.id);
            std::array<glm::vec4,6> planesData;
            for(unsigned i=0;i<6;++i)
            {
                const double length=glm::length(glm::dvec3(frustum.planes[i]));
                planesData[i]=length>0?glm::vec4(frustum.planes[i]/length):glm::vec4(0,0,0,1);
            }
            glUseProgram(cull); glUniform4fv(planes,6,glm::value_ptr(planesData[0])); glUniform3fv(cam,1,glm::value_ptr(camera.camera));
            glUniform4f(lod,settings.lod0Coverage,settings.impostorCoverage,settings.transitionFraction,settings.maximumDistance);
            glUniform1f(projection,camera.projectionScale); glUniform1i(orthographic,camera.orthographic);
            glDispatchCompute(static_cast<GLuint>(n),1,1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT); ++statistics.cullDispatches;
        }
        pass.clip=clip; pass.view=camera; pass.revision=statistics.revision; pass.valid=true;
    }
    void memory()
    {
        statistics.workingBytes=blocks.capacity+instances.capacity+modelBuffer.capacity;
        for(const auto& entry:passes) statistics.workingBytes+=entry.second.selection.capacity+entry.second.commands.capacity+entry.second.visible.capacity;
    }
};

TreeRenderGpu::TreeRenderGpu():m(std::make_unique<Impl>()) {}
TreeRenderGpu::~TreeRenderGpu() { shutdown(); }
bool TreeRenderGpu::initialize(const std::vector<ProceduralTerrain::ObjectPrototype>& prototypes,const RenderSettings& settings)
{
    shutdown(); auto& s=*m; s.failure.clear(); s.settings=settings; s.prototypes=prototypes;
    const float values[]={settings.lod0Coverage,settings.impostorCoverage,settings.transitionFraction,settings.maximumDistance};
    for(float v:values) if(!finiteFieldValue(v)) { s.failure="Non-finite tree rendering setting"; return false; }
    if(prototypes.empty() || prototypes.size()>255 || settings.maximumDistance<=0 || settings.impostorCoverage<=0 ||
        settings.lod0Coverage*(1-settings.transitionFraction)<=settings.impostorCoverage*(1+settings.transitionFraction) ||
        settings.transitionFraction<0 || settings.transitionFraction>0.4f || !settings.modelBudgetMiB || settings.modelBudgetMiB>4096 ||
        !settings.maximumResidentModels || settings.maximumResidentModels>255)
    { s.failure="Invalid tree render settings"; return false; }
    for(const auto& prototype:prototypes)
        if(prototype.asset.empty() || !finiteFieldValue(prototype.minimumScale) || !finiteFieldValue(prototype.maximumScale) ||
            !finiteFieldValue(prototype.boundingRadius) || prototype.minimumScale<=0 ||
            prototype.maximumScale<prototype.minimumScale || prototype.boundingRadius<0)
        { s.failure="Invalid tree model settings"; return false; }
    State state;
    GLint64 storageLimit=0; glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&storageLimit);
    s.storageLimit=static_cast<std::uint64_t>(std::max<GLint64>(0,storageLimit));
    const auto path=[&](const char* name){return settings.shaderDirectory+"/"+name;};
    s.build=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,path("instances.C.glsl"),{}}},s.failure,"Tree transforms",{true,{}});
    if(!s.build) { shutdown(); return false; }
    s.cull=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,path("cull.C.glsl"),{}}},s.failure,"Tree LOD and visibility",{true,{}});
    if(!s.cull) { shutdown(); return false; }
    for(unsigned i=0;i<2;++i)
    {
        auto& p=i?s.shadow:s.opaque; const std::string define=i?"#define TREE_SHADOW 1\n":"#define TREE_SHADOW 0\n";
        p.id=Ge::buildShaderProgram({{GL_VERTEX_SHADER,path("render.vs.glsl"),define},{GL_FRAGMENT_SHADER,path("render.fs.glsl"),define}},s.failure,i?"Tree shadows":"Tree G-buffer",{true,{}});
        if(!p.id) { shutdown(); return false; }
        p.discover();
    }
    s.planes=glGetUniformLocation(s.cull,"uPlanes"); s.cam=glGetUniformLocation(s.cull,"uCamera"); s.lod=glGetUniformLocation(s.cull,"uLod");
    s.projection=glGetUniformLocation(s.cull,"uProjectionScale"); s.orthographic=glGetUniformLocation(s.cull,"uOrthographic");
    s.models.resize(prototypes.size()); s.data.resize(prototypes.size());
    for(unsigned i=0;i<prototypes.size();++i) s.data[i].scales=glm::vec4(prototypes[i].minimumScale,prototypes[i].maximumScale,1,0);
    s.modelTable();
    Mesh quad; quad.vertices.resize(4);
    quad.vertices[0].position={-1,-1,0}; quad.vertices[1].position={1,-1,0}; quad.vertices[2].position={1,1,0}; quad.vertices[3].position={-1,1,0};
    quad.indices={0,1,2,0,2,3};
    if(!s.quad.upload(quad) || s.modelBuffer.capacity!=s.data.size()*sizeof(ModelData)) { s.failure="Cannot initialize tree renderer buffers"; shutdown(); return false; }
    s.initialized=true; return true;
}
void TreeRenderGpu::prepare(unsigned buffer,const std::vector<ProceduralTerrain::ObjectBlock>& blocks,std::uint64_t revision,const RenderView& view)
{
    auto& s=*m; if(!s.initialized) return;
    s.camera=view;
    const bool changed=!s.haveSource || s.sourceBuffer!=buffer || s.sourceRevision!=revision;
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
    State state; auto& pass=s.passes[passID]; s.cache(pass,clip);
    if(passID==0) s.statistics.cameraBlocks=static_cast<unsigned>(pass.blocks.size());
    else s.statistics.shadowBlocks+=static_cast<unsigned>(pass.blocks.size());
    if(!pass.valid || pass.blocks.empty()) return;
    auto& p=passID?s.shadow:s.opaque; glUseProgram(p.id);
    glUniformMatrix4fv(p.clip,1,GL_FALSE,glm::value_ptr(clip)); glUniformMatrix4fv(p.view,1,GL_FALSE,glm::value_ptr(s.camera.view));
    glUniform1f(p.gamma,s.camera.albedoGamma);
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
    glUniform3fv(p.eye,1,glm::value_ptr(eye)); glUniform1i(p.parallel,parallel);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,s.instances.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,s.modelBuffer.id);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,pass.visible.id); glBindBuffer(GL_DRAW_INDIRECT_BUFFER,pass.commands.id);
    for(unsigned i=0;i<3;++i) glBindSampler(i,0);
    for(const auto& group:pass.groups)
    {
        const auto& model=s.models[group.model];
        for(unsigned part=0;part<5;++part)
        {
            const bool billboard=part==4,foliage=(part&1u)!=0;
            if(billboard?!model.albedo:!model.meshes[part].count) continue;
            glUniform1i(p.kind,billboard?3:(foliage?1:0));
            if(billboard || foliage) glDisable(GL_CULL_FACE);
            else { glEnable(GL_CULL_FACE); glCullFace(passID?GL_FRONT:GL_BACK); }
            if(billboard)
            {
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY,model.albedo);
                glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D_ARRAY,model.normal);
                glBindVertexArray(s.quad.vao);
            }
            else { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,foliage?model.leaf:model.bark); glBindVertexArray(model.meshes[part].vao); }
            const auto offset=static_cast<std::uintptr_t>((std::size_t{group.first}*5+part)*CommandBytes);
            glMultiDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_INT,reinterpret_cast<const void*>(offset),group.count,5*CommandBytes);
            ++s.statistics.drawCalls;
        }
    }
    s.memory();
}
void TreeRenderGpu::appendBounds(std::vector<Ge::ShadowCasterBounds>& output) const
{
    for(const auto& b:m->sources) if(m->models[b.prototype].ready)
    {
        const double r=m->models[b.prototype].radius;
        const glm::dvec3 lo(b.origin[0]-r,b.origin[1]-r,b.origin[2]-r);
        output.push_back({lo,lo+glm::dvec3(b.extent+2*r)});
    }
}
void TreeRenderGpu::shutdown() noexcept
{
    if(!m) return; auto& s=*m;
    if(s.loading.valid()) { try { s.loading.wait(); s.loading.get(); } catch(...) {} }
    for(auto& item:s.passes) item.second.clear(); s.passes.clear();
    for(auto& model:s.models) model.clear(); s.models.clear(); s.data.clear();
    s.blocks.clear(); s.instances.clear(); s.modelBuffer.clear(); s.quad.clear();
    for(GLuint p:{s.build,s.cull,s.opaque.id,s.shadow.id}) if(p) glDeleteProgram(p);
    s.build=s.cull=s.opaque.id=s.shadow.id=0; s.sources.clear(); s.wanted.clear();
    s.statistics={}; s.initialized=s.haveSource=s.haveDemand=false; s.sourceBuffer=0;
}
const RenderStats& TreeRenderGpu::stats() const noexcept { return m->statistics; }
const std::string& TreeRenderGpu::error() const noexcept { return m->failure; }
}
