#include "glcore.hpp"
#include "TreeRenderGpu.hpp"
#include "ProceduralTreeBake.hpp"
#include "ProceduralTerrainField.hpp"
#include "ShaderCompilation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace EnduranceTrees;
using namespace ProceduralTerrain;
namespace {
void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
void clean(const char* where) { auto e=glGetError(); require(e==GL_NO_ERROR,std::string(where)+": GL error "+std::to_string(e)); }
struct Command { GLuint count,instances,first; GLint baseVertex; GLuint baseInstance; };
struct Instance { glm::vec4 positionScale; glm::vec2 rotation; GLuint hash,model; };
std::vector<Command> submitted;
std::vector<unsigned> submittedLods;
std::vector<glm::uvec2> references;
std::vector<Instance> transforms;
PFNGLMULTIDRAWELEMENTSINDIRECTPROC realDraw=nullptr;
bool executeDraw=true;
bool foliageOnly=false;
void GLAPIENTRY inspectDraw(GLenum mode,GLenum type,const void* offset,GLsizei draws,GLsizei stride)
{
    GLint commands=0,visible=0,instances=0;
    glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING,&commands);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,7,&visible);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,4,&instances);
    // These blocking reads exist only in this test hook, never in the renderer.
    for(int i=0;i<draws;++i)
    {
        Command command{};
        glGetNamedBufferSubData(commands,reinterpret_cast<std::uintptr_t>(offset)+i*stride,sizeof(command),&command);
        submitted.push_back(command);
        submittedLods.push_back(static_cast<unsigned>((reinterpret_cast<std::uintptr_t>(offset)+i*(stride?stride:sizeof(Command)))/sizeof(Command)%5)/2);
        for(unsigned j=0;j<command.instances;++j)
        {
            glm::uvec2 ref; Instance instance;
            glGetNamedBufferSubData(visible,(command.baseInstance+j)*sizeof(ref),sizeof(ref),&ref);
            glGetNamedBufferSubData(instances,ref.x*sizeof(instance),sizeof(instance),&instance);
            references.push_back(ref); transforms.push_back(instance);
        }
    }
    if(executeDraw && (!foliageOnly || (reinterpret_cast<std::uintptr_t>(offset)/sizeof(Command))%5==1))
        realDraw(mode,type,offset,draws,stride);
}
struct Hook
{
    Hook() { realDraw=__glewMultiDrawElementsIndirect; __glewMultiDrawElementsIndirect=inspectDraw; }
    ~Hook() { __glewMultiDrawElementsIndirect=realDraw; }
};
PFNGLNAMEDBUFFERDATAPROC realBufferData=nullptr;
bool rejectAllocation=false;
void GLAPIENTRY failOneAllocation(GLuint buffer,GLsizeiptr size,const void* data,GLenum usage)
{
    if(rejectAllocation) { rejectAllocation=false; return; }
    realBufferData(buffer,size,data,usage);
}
struct FailedAllocation
{
    FailedAllocation() { realBufferData=__glewNamedBufferData; rejectAllocation=true; __glewNamedBufferData=failOneAllocation; }
    ~FailedAllocation() { __glewNamedBufferData=realBufferData; }
};
struct State
{
    std::vector<GLint64> values;
    State()
    {
        for(auto key:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_SHADER_STORAGE_BUFFER_BINDING,
            GL_DRAW_INDIRECT_BUFFER_BINDING,GL_ACTIVE_TEXTURE,GL_CULL_FACE_MODE,GL_DEPTH_FUNC,
            GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,GL_PIXEL_UNPACK_BUFFER_BINDING,
            GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_IMAGES})
        { GLint v; glGetIntegerv(key,&v); values.push_back(v); }
        for(auto key:{GL_CULL_FACE,GL_DEPTH_TEST,GL_BLEND}) values.push_back(glIsEnabled(key));
        for(unsigned slot:{0,1,2,3,4,5,6,7}) for(auto key:{GL_SHADER_STORAGE_BUFFER_BINDING,GL_SHADER_STORAGE_BUFFER_START,GL_SHADER_STORAGE_BUFFER_SIZE})
        { GLint64 v; glGetInteger64i_v(key,slot,&v); values.push_back(v); }
        GLint active; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        for(unsigned unit=0;unit<4;++unit)
        {
            glActiveTexture(GL_TEXTURE0+unit);
            for(auto key:{GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_2D_ARRAY})
            { GLint v; glGetIntegerv(key,&v); values.push_back(v); }
            GLint v; glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&v); values.push_back(v);
        }
        glActiveTexture(active);
    }
    void check(const char* where) const
    { require(values==State().values,std::string(where)+" leaked GL state"); clean(where); }
};
struct Target
{
    static constexpr int Size=320;
    GLuint fbo=0,colors[4]{},depth=0;
    Target()
    {
        glCreateFramebuffers(1,&fbo); glCreateTextures(GL_TEXTURE_2D,4,colors); glCreateTextures(GL_TEXTURE_2D,1,&depth);
        GLenum formats[]={GL_RG32F,GL_RGB16F,GL_RGBA8,GL_RGB8};
        for(unsigned i=0;i<4;++i)
        { glTextureStorage2D(colors[i],1,formats[i],Size,Size); glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT0+i,colors[i],0); }
        glTextureStorage2D(depth,1,GL_DEPTH_COMPONENT32F,Size,Size); glNamedFramebufferTexture(fbo,GL_DEPTH_ATTACHMENT,depth,0);
        GLenum draws[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
        glNamedFramebufferDrawBuffers(fbo,4,draws);
        require(glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete test target");
    }
    ~Target() { glDeleteFramebuffers(1,&fbo); glDeleteTextures(4,colors); glDeleteTextures(1,&depth); }
    void bind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,Size,Size);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glFrontFace(GL_CCW);
        float zero[]={0,0,0,0},one=1;
        for(unsigned i=0;i<4;++i) glClearNamedFramebufferfv(fbo,GL_COLOR,i,zero);
        glClearNamedFramebufferfv(fbo,GL_DEPTH,0,&one);
    }
    std::vector<unsigned char> colorsRead() const
    {
        std::vector<unsigned char> pixels(Size*Size*4);
        glGetTextureImage(colors[2],0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<GLsizei>(pixels.size()),pixels.data()); return pixels;
    }
    std::vector<float> normalsRead() const
    {
        std::vector<float> pixels(Size*Size*3);
        glGetTextureImage(colors[1],0,GL_RGB,GL_FLOAT,static_cast<GLsizei>(pixels.size()*4),pixels.data()); return pixels;
    }
    std::vector<float> depthRead() const
    {
        std::vector<float> pixels(Size*Size);
        glGetTextureImage(depth,0,GL_DEPTH_COMPONENT,GL_FLOAT,static_cast<GLsizei>(pixels.size()*4),pixels.data()); return pixels;
    }
    // A fixed diffuse inspection light, not an engine screenshot. This makes
    // the canopy-normal change visible in optional validation captures.
    void captureLighting(const char* name) const
    {
        const char* directory=std::getenv("ENDURANCE_TREE_TEST_CAPTURE_DIR"); if(!directory) return;
        auto color=colorsRead(); auto normal=normalsRead();
        const glm::vec3 light=glm::normalize(glm::vec3(-.3f,1,.4f));
        std::ofstream out(std::filesystem::path(directory)/(std::string(name)+".ppm"),std::ios::binary);
        out<<"P6\n"<<Size<<' '<<Size<<"\n255\n";
        for(int y=Size-1;y>=0;--y) for(int x=0;x<Size;++x)
        {
            const int i=y*Size+x; const glm::vec3 n(normal[i*3],normal[i*3+1],normal[i*3+2]);
            const float shade=.18f+.82f*std::max(glm::dot(n,light),0.0f);
            for(int c=0;c<3;++c) out.put(static_cast<unsigned char>(color[i*4+c]*shade));
        }
    }
    unsigned checkGBuffer(const char* name,bool expect=true) const
    {
        auto color=colorsRead(); unsigned count=0;
        std::vector<float> normal(Size*Size*3),position(Size*Size*2);
        std::vector<unsigned char> material(Size*Size*3);
        glGetTextureImage(colors[0],0,GL_RG,GL_FLOAT,static_cast<GLsizei>(position.size()*4),position.data());
        glGetTextureImage(colors[1],0,GL_RGB,GL_FLOAT,static_cast<GLsizei>(normal.size()*4),normal.data());
        glGetTextureImage(colors[3],0,GL_RGB,GL_UNSIGNED_BYTE,static_cast<GLsizei>(material.size()),material.data());
        for(int i=0;i<Size*Size;++i) if(color[i*4+3])
        {
            ++count; glm::vec3 n(normal[i*3],normal[i*3+1],normal[i*3+2]);
            require(finiteFieldValue(position[i*2]) && position[i*2]<0,"Invalid signed G-buffer view Z");
            require(std::abs(glm::length(n)-1)<.01f,"Tree normals are not unit length");
            require(material[i*3]==0 && material[i*3+1]>=200 && material[i*3+2]==255,"Wrong tree PBR channels");
        }
        if(expect) require(count>0,std::string(name)+" produced no visible pixels");
        if(const char* directory=std::getenv("ENDURANCE_TREE_TEST_CAPTURE_DIR"))
        {
            std::ofstream out(std::filesystem::path(directory)/(std::string(name)+".ppm"),std::ios::binary);
            out<<"P6\n"<<Size<<' '<<Size<<"\n255\n";
            for(int y=Size-1;y>=0;--y) for(int x=0;x<Size;++x) out.write(reinterpret_cast<char*>(color.data()+(y*Size+x)*4),3);
        }
        clean(name); return count;
    }
    unsigned depthPixels() const
    {
        std::vector<float> values(Size*Size); glGetTextureImage(depth,0,GL_DEPTH_COMPONENT,GL_FLOAT,static_cast<GLsizei>(values.size()*4),values.data());
        return static_cast<unsigned>(std::count_if(values.begin(),values.end(),[](float v){return v<1;}));
    }
};
RenderView viewAt(glm::vec3 target,float distance,float pitch=0,float yaw=0)
{
    RenderView v; auto projection=glm::perspective(glm::radians(60.0f),1.0f,.1f,8000.0f);
    v.camera=target+glm::vec3(std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch))*distance;
    v.view=glm::lookAt(v.camera,target,std::abs(pitch)>1.56f?glm::vec3(0,0,-1):glm::vec3(0,1,0));
    v.clip=projection*v.view; v.projectionScale=projection[1][1]; return v;
}
void draw(TreeRenderGpu& gpu,Target& target,unsigned pass,const glm::mat4& clip)
{
    target.bind(); submitted.clear(); submittedLods.clear(); references.clear(); transforms.clear();
    const State before; gpu.draw(pass,clip); before.check("draw");
}
void waitAssets(TreeRenderGpu& gpu,GLuint pool,const std::vector<ObjectBlock>& blocks,std::uint64_t revision,const RenderView& view,unsigned models)
{
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    do
    {
        const State before; gpu.prepare(pool,blocks,revision,view); before.check("prepare");
        require(gpu.error().empty(),gpu.error());
        if(gpu.stats().loadedModels>=models && !gpu.stats().pendingModels) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("Timed out waiting for baked assets");
}
std::array<unsigned,3> lodCounts()
{
    std::array<unsigned,3> result{};
    for(unsigned i=0;i<submitted.size();++i)
        if(submitted[i].count) result[submittedLods[i]]+=submitted[i].instances;
    return result;
}
}

extern "C" int tree_runtime_tests(const char* root,const char* temporary)
{
    try
    {
        glewExperimental=GL_TRUE; auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW failed");
        while(glGetError()!=GL_NO_ERROR) {}
        Hook hook; Target target;
        const auto asset=std::filesystem::path(root)/"ENDURANCE/Ressources/Trees/Baked/Acacia/tree_1_seed_1371";
        Tree tree; std::vector<ImpostorView> views; std::string error;
        require(loadBakedTree(asset,tree,error,&views),error); require(views.size()==17,"Fixture lacks 17 impostors");
        const float radius=glm::length(tree.boundsMax-tree.boundsMin)*.5f;
        ObjectPrototype prototype; prototype.asset=asset.string(); prototype.minimumScale=prototype.maximumScale=1;
        prototype.boundingRadius=radius*2;
        RenderSettings settings; settings.shaderDirectory=(std::filesystem::path(root)/"ENDURANCE/Ressources/Shader/Trees").string();
        settings.maximumDistance=3000; settings.modelBudgetMiB=64;
        settings.windEnabled=false; settings.foliageNormalBlend=0;
        TreeRenderGpu gpu; require(gpu.initialize({prototype},settings),gpu.error());
        GLuint pool=0,foreign=0; glCreateBuffers(1,&pool); glCreateBuffers(1,&foreign);
        glNamedBufferData(foreign,4096,nullptr,GL_DYNAMIC_COPY);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER,5,foreign,256,1024);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER,foreign); glActiveTexture(GL_TEXTURE3);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,foreign); glPixelStorei(GL_UNPACK_ROW_LENGTH,7); glPixelStorei(GL_UNPACK_SKIP_ROWS,2);
        PackedObjectPosition position{32768u|(32768u<<16u),32768u|(42u<<16u)};
        std::vector<PackedObjectPosition> positions(512,position);
        glNamedBufferData(pool,positions.size()*sizeof(position),positions.data(),GL_DYNAMIC_DRAW);
        ObjectBlock block; block.instanceCount=1; block.origin={{-64,-64,-64}}; block.extent=128; block.seed=1234;
        std::vector<ObjectBlock> blocks{block}; std::uint64_t revision=1;
        auto view=viewAt({0,7,0},radius*4);
        waitAssets(gpu,pool,blocks,revision,view,1);
        require(gpu.stats().assetUploads==1 && gpu.stats().transformBuilds==1,"Assets or transforms rebuilt while idle");
        const auto drawCallsBefore=gpu.stats().drawCalls;
        draw(gpu,target,0,view.clip); require(!transforms.empty(),"No instanced draw");
        require(gpu.stats().drawCalls-drawCallsBefore==3,"Geometric LODs were not grouped into three material draws");
        Instance original=transforms[0]; auto centre=(tree.boundsMin+tree.boundsMax)*.5f;
        auto r=original.rotation;
        centre=glm::vec3(original.positionScale)+glm::vec3(r.x*centre.x+r.y*centre.z,centre.y,-r.y*centre.x+r.x*centre.z);
        const auto run=[&](float coverage,const char* label)
        {
            view=viewAt(centre,radius*1.7320508f/coverage);
            gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
            target.checkGBuffer(label); return lodCounts();
        };
        require(run(.5f,"lod0")==std::array<unsigned,3>{{2,0,0}},"Close tree did not choose LOD0");
        require(run(.12f,"lod1")==std::array<unsigned,3>{{0,2,0}},"Middle tree did not choose LOD1");
        require(run(.035f,"lod3")==std::array<unsigned,3>{{0,0,1}},"Far tree did not choose impostor");
        for(float boundary:{settings.lod0Coverage,settings.impostorCoverage})
        {
            auto counts=run(boundary,boundary>.1f?"transition01":"transition13");
            require(counts[1]==2 && (counts[0]==2 || counts[2]==1),"LOD transition did not draw adjacent levels");
            bool fine=false,coarse=false;
            for(auto ref:references) { if(ref.y&256u) coarse=true; else fine=true; require(std::abs(int(ref.y&255u)-128)<=2,"Bad fade weight"); }
            require(fine&&coarse,"LOD fades are not complementary");
        }
        const auto before=gpu.stats();
        for(int i=0;i<3;++i) { gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip); }
        require(gpu.stats().cullDispatches==before.cullDispatches && gpu.stats().transformBuilds==before.transformBuilds &&
            gpu.stats().assetUploads==before.assetUploads,"Unchanged view repeats compute/upload work");
        const auto selectedUploads=gpu.stats().selectionUploads;
        view.camera.x+=.01f; gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(gpu.stats().selectionUploads==selectedUploads,"Camera LOD change uploaded identical selected-page metadata");
        // A pool relocation must not reseed or move the same tree.
        blocks[0].firstInstance=64; ++revision; gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(!transforms.empty() && std::memcmp(&original,&transforms[0],sizeof(Instance))==0,"Pool relocation changed tree transform");
        // New publications rebuild only genuinely new/recycled immutable pages.
        blocks[0].allocationRevision=1001; ++revision; gpu.prepare(pool,blocks,revision,view);
        draw(gpu,target,0,view.clip);
        const auto identicalBefore=gpu.stats();
        ++revision; gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(gpu.stats().revision==identicalBefore.revision && gpu.stats().cullDispatches==identicalBefore.cullDispatches &&
            gpu.stats().selectionUploads==identicalBefore.selectionUploads,"Identical immutable publication rebuilt command caches");
        const auto cachedBefore=gpu.stats();
        for(unsigned page=0;page<8;++page) if(page!=1)
        { auto added=block; added.firstInstance=page*64; added.allocationRevision=1000+page; blocks.push_back(added); }
        ++revision; gpu.prepare(pool,blocks,revision,view);
        require(gpu.stats().transformedInstances-cachedBefore.transformedInstances==7 &&
            gpu.stats().transformPagesReused>cachedBefore.transformPagesReused,"Existing pages were rebuilt during incremental publication");
        executeDraw=false; draw(gpu,target,0,view.clip); executeDraw=true;
        require(submitted.size()==5,"Commands were not merged across pages of one model");
        for(const auto& instance:transforms) require(std::memcmp(&instance,&original,sizeof(Instance))==0,"Transform buffer growth lost a cached page");
        blocks.resize(1); ++revision; auto builds=gpu.stats().transformBuilds; gpu.prepare(pool,blocks,revision,view);
        require(gpu.stats().transformBuilds==builds,"Page removal rebuilt remaining transforms");
        // A recycled allocation at the same address, with identical metadata,
        // must refresh bytes; allocation identity is independent of pool index.
        positions[64].xy+=1024u; glNamedBufferSubData(pool,64*sizeof(position),sizeof(position),&positions[64]);
        ++blocks[0].allocationRevision; ++revision; gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(!transforms.empty() && transforms[0].positionScale.x>original.positionScale.x+1.0f,"Recycled allocation kept stale coordinates");
        positions[64]=position; glNamedBufferSubData(pool,64*sizeof(position),sizeof(position),&positions[64]);
        ++blocks[0].allocationRevision; ++revision; gpu.prepare(pool,blocks,revision,view);
        // Face away: camera rejects the tree, light still sees it.
        view=viewAt(centre,radius*6); view.view=glm::lookAt(view.camera,view.camera+(view.camera-centre),glm::vec3(0,1,0));
        view.clip=glm::perspective(glm::radians(60.f),1.f,.1f,8000.f)*view.view;
        gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(target.depthPixels()==0,"Off-screen tree leaked into camera");
        const auto light=glm::ortho(-radius*1.1f,radius*1.1f,-radius*1.1f,radius*1.1f,.1f,radius*10)*
            glm::lookAt(centre+glm::vec3(radius*2,radius*3,radius*2),centre,glm::vec3(0,1,0));
        draw(gpu,target,1,light); require(target.depthPixels()>0,"Off-camera shadow caster disappeared");
        const auto shadowBuilds=gpu.stats().cullDispatches; draw(gpu,target,1,light);
        require(gpu.stats().cullDispatches==shadowBuilds,"Static shadow culling not reused");
        const auto distanceSelections=gpu.stats().distanceSelections;
        const State beforeTokens;
        const auto content=gpu.shadowContentRevision(1,light);
        require(content!=0 && gpu.shadowContentRevision(1,light)==content,"Unchanged shadow content token is unstable");
        beforeTokens.check("CPU-only shadow token");
        require(gpu.stats().distanceSelections==distanceSelections,"Shadow token repeated the common camera-distance selection");
        // A far publication shifts CPU page indices but must not invalidate a
        // layer whose physical pages and loaded model are exactly unchanged.
        auto farBlock=blocks[0]; farBlock.firstInstance=128; farBlock.origin[0]+=10000;
        farBlock.allocationRevision=7000; blocks.insert(blocks.begin(),farBlock);
        ++revision; gpu.prepare(pool,blocks,revision,view);
        require(gpu.shadowContentRevision(1,light)==content,"Unrelated tree publication dirtied a shadow layer");
        ++blocks[1].allocationRevision; ++revision; gpu.prepare(pool,blocks,revision,view);
        const auto recycledContent=gpu.shadowContentRevision(1,light);
        require(recycledContent!=content,"Recycled visible page did not dirty the shadow layer");
        blocks.erase(blocks.begin()); ++revision; gpu.prepare(pool,blocks,revision,view);
        require(gpu.shadowContentRevision(1,light)==recycledContent,"Removal of an unrelated page dirtied the layer");
        const auto emptyClip=glm::translate(light,glm::vec3(10000,0,0));
        const auto emptyToken=gpu.shadowContentRevision(10,emptyClip);
        view.camera.x+=.1f; gpu.prepare(pool,blocks,revision,view);
        require(gpu.shadowContentRevision(10,emptyClip)==emptyToken,"Camera LOD changes dirty an empty shadow layer");
        require(gpu.shadowContentRevision(1,light)!=recycledContent,"Camera LOD changes failed to invalidate visible tree shadows");
        // An allocation failure after the engine sampled a valid content token
        // must not freeze a missing shadow. Retry with identical inputs.
        require(gpu.shadowContentRevision(12,light)!=0,"Missing initial shadow token");
        { FailedAllocation failure; draw(gpu,target,12,light); }
        require(target.depthPixels()==0 && !gpu.error().empty() && gpu.shadowContentRevision(12,light)==0,
            "Failed visibility allocation remained cacheable");
        draw(gpu,target,12,light);
        require(target.depthPixels()>0 && gpu.error().empty() && gpu.shadowContentRevision(12,light)!=0,
            "Visibility allocation did not recover with unchanged inputs");
        // Exercise all azimuths and the zenith, with larger inspection captures.
        for(unsigned index:{0u,3u,7u,8u,12u,16u})
        {
            const auto direction=impostorDirection(index);
            view=viewAt(centre,radius*4,std::asin(direction.y),std::atan2(direction.x,direction.z));
            view.projectionScale=.05f; gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
            require(lodCounts()[2]==1,"Impostor inspection failed"); target.checkGBuffer(("view"+std::to_string(index)).c_str());
        }
        draw(gpu,target,2,light);
        require(lodCounts()[2]==1 && target.depthPixels()>0 && target.depthPixels()<Target::Size*Target::Size/2,
            "Impostor shadow is absent or its alpha cutout became an opaque quad");
        const auto spot=viewAt(centre,radius*4,.5f,.6f);
        draw(gpu,target,3,spot.clip);
        require(target.depthPixels()>0 && target.depthPixels()<Target::Size*Target::Size/2,"Perspective impostor shadow failed");
        // An orthographic viewpoint must use its screen scale, not camera distance.
        view=viewAt(centre,radius*4); view.orthographic=true; view.projectionScale=.12f/radius;
        view.clip=glm::ortho(-radius*2,radius*2,-radius*2,radius*2,.1f,radius*10)*view.view;
        gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(lodCounts()==std::array<unsigned,3>{{0,2,0}},"Orthographic LOD uses distance"); target.checkGBuffer("ortho");
        view=viewAt(centre,settings.maximumDistance*1.1f); gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(target.depthPixels()==0,"Far distance cutoff failed");
        std::vector<Ge::ShadowCasterBounds> bounds; gpu.appendBounds(bounds); require(!bounds.empty(),"Missing caster bounds");
        blocks.clear(); ++revision; gpu.prepare(pool,blocks,revision,view); draw(gpu,target,0,view.clip);
        require(submitted.empty() && gpu.stats().instances==0,"Unloaded sector still renders");
        gpu.shutdown(); gpu.shutdown();
        // Full pages, interleaved model IDs, nonzero baseInstance and scale-aware LOD.
        ObjectPrototype large=prototype; large.minimumScale=large.maximumScale=2;
        require(gpu.initialize({prototype,large},settings),gpu.error());
        auto second=block; second.prototype=1; second.firstInstance=64;
        blocks={second,block}; blocks[0].instanceCount=blocks[1].instanceCount=64;
        for(unsigned i=0;i<positions.size();++i) positions[i].zSite=32768u|((i%64u)<<16u);
        glNamedBufferSubData(pool,0,positions.size()*sizeof(position),positions.data());
        view=viewAt({0,7,0},radius*1.7320508f/.17f);
        waitAssets(gpu,pool,blocks,1,view,2);
        executeDraw=false; draw(gpu,target,0,view.clip); executeDraw=true;
        require(lodCounts()==std::array<unsigned,3>{{128,128,0}},"Full-page compaction or per-model scale LOD failed");
        require(gpu.stats().instances==128 && submitted.size()==10,"Multi-model indirect grouping failed");
        for(const auto& command:submitted) require(command.instances<=64,"Visibility page overflow");
        for(const auto& instance:transforms)
            require(instance.model<2 && instance.positionScale.w==float(instance.model+1),"Indirect draw references the wrong model");
        auto third=block; third.firstInstance=128; third.instanceCount=64; blocks.push_back(third);
        gpu.prepare(pool,blocks,2,view); executeDraw=false; draw(gpu,target,0,view.clip); executeDraw=true;
        require(submitted.size()==10 && lodCounts()==std::array<unsigned,3>{{128,256,0}},
            "Per-model compaction did not merge multiple pages or overlapped its LOD ranges");
        blocks.resize(2);
        // Actually raster several instances with distinct positions, not just the commands.
        blocks[0].instanceCount=blocks[1].instanceCount=3;
        for(unsigned i=0;i<3;++i) for(unsigned page=0;page<2;++page)
            positions[page*64+i].xy=(positions[page*64+i].xy&0xffff0000u)|(18000u+i*14000u);
        glNamedBufferSubData(pool,0,positions.size()*sizeof(position),positions.data());
        gpu.prepare(pool,blocks,3,view); draw(gpu,target,0,view.clip); target.checkGBuffer("instances");
        require(lodCounts()==std::array<unsigned,3>{{6,6,0}},"Multiple instanced draws failed");
        gpu.shutdown();
        // Cache eviction follows camera demand, without exceeding the model limit.
        settings.maximumResidentModels=1; require(gpu.initialize({prototype,large},settings),gpu.error());
        blocks={block,second}; blocks[1].origin[0]+=512; blocks[1].sectorLattice[0]=512;
        view=viewAt({0,7,0},radius*4); waitAssets(gpu,pool,blocks,1,view,1);
        draw(gpu,target,0,view.clip); require(!transforms.empty() && transforms[0].model==0,"Initial cache priority incorrect");
        view=viewAt({512,7,0},radius*4); gpu.prepare(pool,blocks,1,view);
        waitAssets(gpu,pool,blocks,1,view,1); draw(gpu,target,0,view.clip);
        require(!transforms.empty() && transforms[0].model==1 && gpu.stats().loadedModels==1 && gpu.stats().assetUploads==2,
            "Model eviction did not follow the camera"); gpu.shutdown();
        settings.maximumResidentModels=64; settings.modelBudgetMiB=1;
        require(gpu.initialize({prototype},settings),gpu.error()); blocks={block}; view=viewAt({0,7,0},radius*4);
        for(int i=0;i<1000 && !gpu.stats().budgetSkippedModels;++i)
        { gpu.prepare(pool,blocks,1,view); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        require(gpu.stats().budgetSkippedModels==1 && gpu.stats().modelBytes==0,"Oversize asset bypassed memory budget");
        for(int i=0;i<10;++i) gpu.prepare(pool,blocks,1,view);
        require(!gpu.stats().pendingModels && !gpu.stats().assetUploads,"Oversize asset reloads every frame");
        gpu.shutdown(); settings.modelBudgetMiB=64;
        // Transition-disabled settings compile a distinct cull mode, rather
        // than a per-instance branch. Boundary trees select exactly one LOD.
        settings.transitionFraction=0;
        require(gpu.initialize({prototype},settings),gpu.error()); blocks={block};
        view=viewAt(centre,radius*1.7320508f/settings.lod0Coverage);
        waitAssets(gpu,pool,blocks,1,view,1); draw(gpu,target,0,view.clip);
        const auto noFade=lodCounts();
        require(noFade==std::array<unsigned,3>{{2,0,0}} || noFade==std::array<unsigned,3>{{0,2,0}},
            "Transition-disabled shader emitted two LODs");
        gpu.shutdown(); settings.transitionFraction=.15f;
        // Existing bakes: canopy normals change only normals, never geometry,
        // coverage, colors or wood shading. Both adjacent geometric LODs work.
        positions.assign(512,position); glNamedBufferSubData(pool,0,positions.size()*sizeof(position),positions.data());
        blocks={block}; blocks[0].allocationRevision=9100;
        view=viewAt(centre,radius*4);
        require(gpu.initialize({prototype},settings),gpu.error()); waitAssets(gpu,pool,blocks,1,view,1);
        draw(gpu,target,0,view.clip); target.captureLighting("canopy_before");
        const auto originalColors=target.colorsRead(); const auto originalNormals=target.normalsRead(),originalDepth=target.depthRead();
        std::vector<unsigned char> originalPbr(Target::Size*Target::Size*3);
        glGetTextureImage(target.colors[3],0,GL_RGB,GL_UNSIGNED_BYTE,originalPbr.size(),originalPbr.data());
        std::vector<Ge::ShadowCasterBounds> originalBounds; gpu.appendBounds(originalBounds);
        view.projectionScale=.065f*glm::length(view.camera-centre)/radius;
        gpu.prepare(pool,blocks,1,view); draw(gpu,target,0,view.clip);
        const auto transitionColors=target.colorsRead(); const auto transitionNormals=target.normalsRead(),transitionDepth=target.depthRead();
        gpu.shutdown(); settings.foliageNormalBlend=.8f;
        view=viewAt(centre,radius*4);
        require(gpu.initialize({prototype},settings),gpu.error()); waitAssets(gpu,pool,blocks,1,view,1);
        draw(gpu,target,0,view.clip); target.checkGBuffer("canopy"); target.captureLighting("canopy_after");
        const auto roundedNormals=target.normalsRead(); unsigned changedLeaves=0,unchangedWood=0;
        require(target.colorsRead()==originalColors && target.depthRead()==originalDepth,"Canopy normals modified geometry/albedo");
        for(unsigned pixel=0;pixel<originalColors.size()/4;++pixel) if(originalColors[pixel*4+3])
        {
            const float d=glm::length(glm::vec3(roundedNormals[pixel*3]-originalNormals[pixel*3],
                roundedNormals[pixel*3+1]-originalNormals[pixel*3+1],roundedNormals[pixel*3+2]-originalNormals[pixel*3+2]));
            if(originalPbr[pixel*3+1]>220) { require(d<1e-5f,"Canopy correction changed wood normals"); ++unchangedWood; }
            else if(d>.05f) ++changedLeaves;
        }
        require(changedLeaves>100 && unchangedWood>10,"Canopy normal correction was not exercised");
        gpu.shutdown(); settings.windEnabled=true; settings.windStrength=.05f; settings.windScale=8;
        require(gpu.initialize({prototype},settings),gpu.error()); waitAssets(gpu,pool,blocks,1,view,1);
        draw(gpu,target,0,view.clip); target.checkGBuffer("wind0"); const auto windDepth0=target.depthRead();
        draw(gpu,target,21,light); const auto windShadow0=target.depthRead();
        const auto movingToken=gpu.shadowContentRevision(21,light),emptyWindToken=gpu.shadowContentRevision(22,emptyClip);
        const auto windStats=gpu.stats();
        view.timeSeconds=9; gpu.prepare(pool,blocks,1,view); draw(gpu,target,0,view.clip); target.checkGBuffer("wind9");
        require(target.depthRead()!=windDepth0,"Wind did not animate the rendered tree");
        draw(gpu,target,21,light);
        require(target.depthRead()!=windShadow0 && gpu.shadowContentRevision(21,light)!=movingToken,"Wind left a frozen shadow");
        require(gpu.shadowContentRevision(22,emptyClip)==emptyWindToken,"Wind invalidated an empty shadow layer");
        require(gpu.stats().transformBuilds==windStats.transformBuilds && gpu.stats().cullDispatches==windStats.cullDispatches &&
            gpu.stats().selectionUploads==windStats.selectionUploads && gpu.stats().revision==windStats.revision,
            "Animation rebuilt static transforms/visibility/publication");
        const auto heldToken=gpu.shadowContentRevision(21,light);
        gpu.prepare(pool,blocks,1,view);
        require(gpu.shadowContentRevision(21,light)==heldToken,"Unchanged animation phase invalidated shadow cache");
        std::vector<Ge::ShadowCasterBounds> sweptBounds; gpu.appendBounds(sweptBounds);
        require(sweptBounds.size()==1 && glm::all(glm::lessThan(sweptBounds[0].minimum,originalBounds[0].minimum)) &&
            glm::all(glm::greaterThan(sweptBounds[0].maximum,originalBounds[0].maximum)),"Wind caster bounds exclude swept geometry");
        // Same main-camera fades and same deformation in color and shadow;
        // exclude wood here because the engine deliberately culls opposite faces.
        foliageOnly=true; draw(gpu,target,0,view.clip); const auto leafDepth=target.depthRead();
        draw(gpu,target,23,view.clip); require(target.depthRead()==leafDepth,"Wind geometry differs between color and shadow passes");
        foliageOnly=false;
        view.projectionScale=.065f*glm::length(view.camera-centre)/radius;
        gpu.prepare(pool,blocks,1,view); draw(gpu,target,0,view.clip);
        const auto fadedNormals=target.normalsRead(),fadedDepth=target.depthRead();
        float normalError=0,depthError=0;
        for(unsigned i=0;i<fadedNormals.size();++i) normalError=std::max(normalError,std::abs(fadedNormals[i]-transitionNormals[i]));
        for(unsigned i=0;i<fadedDepth.size();++i) depthError=std::max(depthError,std::abs(fadedDepth[i]-transitionDepth[i]));
        require(target.colorsRead()==transitionColors && depthError<1e-7f && normalError<.002f,
            "Wind/canopy correction was not fully faded before the baked-impostor transition; normal="+std::to_string(normalError)+
            " depth="+std::to_string(depthError)+" colorEqual="+std::to_string(target.colorsRead()==transitionColors));
        std::cout<<"Impostor transition: max normal error "<<normalError<<", max depth error "<<depthError<<'\n';
        view.projectionScale=.12f*glm::length(view.camera-centre)/radius;
        gpu.prepare(pool,blocks,1,view); draw(gpu,target,0,view.clip); target.checkGBuffer("wind_lod1");
        require(lodCounts()==std::array<unsigned,3>{{0,2,0}},"Animated LOD1 was not exercised");
        const auto lod1WindDepth=target.depthRead(); view.timeSeconds+=5;
        gpu.prepare(pool,blocks,1,view); draw(gpu,target,0,view.clip);
        require(target.depthRead()!=lod1WindDepth,"LOD1 wind stopped prematurely");
        view.orthographic=true; view.projectionScale=.01f/radius;
        gpu.prepare(pool,blocks,1,view); const auto impostorOnlyToken=gpu.shadowContentRevision(21,light);
        view.timeSeconds+=3; gpu.prepare(pool,blocks,1,view);
        require(gpu.shadowContentRevision(21,light)==impostorOnlyToken,"Impostor-only layer invalidated by wind");
        view=viewAt(centre,settings.windDistance+2*radius+2*block.extent);
        gpu.prepare(pool,blocks,1,view); const auto farWindToken=gpu.shadowContentRevision(21,light);
        view.timeSeconds+=3; gpu.prepare(pool,blocks,1,view);
        require(gpu.shadowContentRevision(21,light)==farWindToken,"Distant static trees still invalidate shadows for wind");
        gpu.shutdown(); settings.windEnabled=false; settings.foliageNormalBlend=0;
        // Production deformation function: anchored roots, quadratic height,
        // scale-aware bounded displacement and inverse-transpose normals.
        const auto probe=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,(std::filesystem::path(root)/"Tests/Trees/deformation_probe.C.glsl").string(),{}}},error);
        require(probe!=0,error); GLuint probeBuffer=0; glCreateBuffers(1,&probeBuffer);
        glNamedBufferData(probeBuffer,6*sizeof(glm::vec4),nullptr,GL_DYNAMIC_READ);
        glUseProgram(probe); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,probeBuffer); glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT); std::array<glm::vec4,6> results{};
        glGetNamedBufferSubData(probeBuffer,0,sizeof(results),results.data());
        require(glm::length(glm::vec3(results[0]))<1e-6f && glm::length(glm::vec3(results[3]))<1e-6f,"Wind moved roots/below-ground vertices");
        require(std::abs(results[1].x-1)<1e-5f && std::abs(results[2].x-4)<1e-5f,"Wind height/scale envelope is incorrect");
        require(std::abs(glm::dot(glm::vec3(results[4]),glm::vec3(results[5])))<.001f,"Wind normal is not perpendicular to the bent surface");
        glUseProgram(0); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0); glDeleteBuffers(1,&probeBuffer); glDeleteProgram(probe);
        // Old bakes without LOD3 retain the simplified mesh; bad assets fail once.
        auto small=generate(preset(Family::Broadleaf)); const auto fallback=std::filesystem::path(temporary)/"no-impostor";
        require(bake(small,fallback,error),error); prototype.asset=fallback.string();
        require(gpu.initialize({prototype},settings),gpu.error()); blocks={block}; view=viewAt({0,7,0},500);
        waitAssets(gpu,pool,blocks,1,view,1); draw(gpu,target,0,view.clip);
        require(lodCounts()==std::array<unsigned,3>{{0,2,0}},"Missing impostor has no LOD1 fallback");
        target.checkGBuffer("fallback"); gpu.shutdown();
        prototype.asset=(std::filesystem::path(temporary)/"missing").string(); require(gpu.initialize({prototype},settings),gpu.error());
        for(int i=0;i<200 && gpu.stats().failedModels==0;++i)
        { gpu.prepare(pool,blocks,1,view); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        require(gpu.stats().failedModels==1 && !gpu.error().empty(),"Bad asset failure not reported");
        for(int i=0;i<10;++i) gpu.prepare(pool,blocks,1,view);
        require(gpu.stats().failedModels==1 && !gpu.stats().pendingModels,"Bad asset retried every frame");
        gpu.shutdown();
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glPixelStorei(GL_UNPACK_ROW_LENGTH,0); glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
        glDeleteBuffers(1,&pool); glDeleteBuffers(1,&foreign); clean("shutdown");
        std::cout<<"Tree runtime: real bake, LOD0/1/3, fades, GPU commands, stable transforms, full pages, scale LOD, 17-view selection, G-buffer, canopy normals, wind/root anchoring, animated shadows, state, cache eviction, budgets and fallback OK\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<"Tree runtime: "<<e.what()<<'\n'; return 1; }
}
