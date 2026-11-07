// Integration fixture: real engine camera/light/terrain managers in a surfaceless
// EGL context, without a window, BehaviourManager update, or gameplay scripts.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "glcore.hpp"
#include <glm/gtc/matrix_transform.hpp>
// Test-only access to manager initialization and light-matrix fixtures. No
// production type layout, virtual function or method implementation is changed.
#define protected public
#include "CameraManager.hpp"
#include "LightManager.hpp"
#include "DirectionalLight.hpp"
#include "PointLight.hpp"
#include "SpotLight.hpp"
#undef protected
#include "ProceduralGeneration.hpp"
#include "ProceduralTerrainLod.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "ProjectData.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipelineManager.hpp"

// The Linux runtime test needs libEGL.so.1, but no optional EGL dev headers.
extern "C" {
void* eglGetDisplay(void*);
unsigned eglInitialize(void*,int*,int*);
unsigned eglBindAPI(unsigned);
unsigned eglChooseConfig(void*,const int*,void**,int,int*);
void* eglCreateContext(void*,void*,void*,const int*);
unsigned eglMakeCurrent(void*,void*,void*,void*);
unsigned eglDestroyContext(void*,void*);
unsigned eglTerminate(void*);
}
namespace {
void require(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
struct Context {
    void* display=nullptr;void* context=nullptr;
    Context() {
        display=eglGetDisplay(nullptr);int major=0,minor=0;
        require(display && eglInitialize(display,&major,&minor),"EGL unavailable");
        require(eglBindAPI(0x30A2),"Desktop OpenGL unavailable");
        const int configAttributes[]={0x3033,1,0x3040,8,0x3038};void* config=nullptr;int count=0;
        require(eglChooseConfig(display,configAttributes,&config,1,&count) && count,"No EGL config");
        const int attributes[]={0x3098,4,0x30FB,5,0x30FD,1,0x3038};
        context=eglCreateContext(display,config,nullptr,attributes);
        require(context && eglMakeCurrent(display,nullptr,nullptr,context),"EGL context unavailable");
        glewExperimental=GL_TRUE;const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW failed");
        require(GLEW_VERSION_4_5,"OpenGL 4.5 required");
        while(glGetError()!=GL_NO_ERROR) {}
        std::cout << "OpenGL " << glGetString(GL_VERSION) << "; " << glGetString(GL_RENDERER) << '\n';
    }
    ~Context() { if(display) {eglMakeCurrent(display,nullptr,nullptr,nullptr);if(context)eglDestroyContext(display,context);eglTerminate(display);} }
};
void settle(ProceduralGeneration& terrain) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    for(unsigned frame=0;frame<10000 && std::chrono::steady_clock::now()<deadline;++frame) {
        terrain.prepareRenderer();glFinish(); // Explicit completion is test-only.
        require(!terrain.getStats().resourceLimited,"Terrain resource failure: "+terrain.getLastError());
        require(glGetError()==GL_NO_ERROR,"OpenGL error during terrain preparation");
        if(!terrain.getStats().rebuilding && terrain.getStats().pendingJobs==0) return;
    }
    throw std::runtime_error("Terrain did not converge: "+terrain.getLastError());
}
std::size_t expectedSelection(const glm::vec3& position,const ProceduralGeneration::Config& config) {
    ProceduralTerrain::LodConfig lod;
    lod.baseSpacing=config.cellSize;lod.maxLod=config.maxLod;lod.viewDistance=config.viewDistance;
    lod.splitDistanceFactor=config.lodDistanceFactor;lod.maxLeaves=config.maxSelectedChunks;
    lod.fullResolutionRadius=0;
    auto bounds=ProceduralTerrain::fieldHeightBounds(config.field);
    const float padding=std::ldexp(config.cellSize,int(config.maxLod));
    lod.minimumY=bounds.minimumY-padding;lod.maximumY=bounds.maximumY+padding;
    std::vector<ProceduralTerrain::LodChunk> chunks;std::string error;
    require(ProceduralTerrain::selectLodChunks(position,lod,chunks,error),error);
    return chunks.size();
}
void shadowAttachments(Ge::LightManager& lights,unsigned expected) {
    const auto& framebuffers=lights.getFrameShadowBuffer();
    require(framebuffers.size()==expected,"Wrong number of shadow framebuffer layers");
    for(unsigned layer=0;layer<expected;++layer) {
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffers[layer]);
        GLint texture=0,attachedLayer=-1;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&texture);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER,&attachedLayer);
        require(static_cast<unsigned>(texture)==lights.getTextureShadowArray() && attachedLayer==int(layer),"Shadow framebuffer retained a stale texture or layer");
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Shadow framebuffer incomplete after changing lights");
    }
    require(glGetError()==GL_NO_ERROR,"OpenGL error during real shadow framebuffer lifecycle");
}
struct Target {
    GLuint framebuffer=0,depth=0;std::array<GLuint,4> color{};
    Target() {
        glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glGenTextures(4,color.data());
        for(unsigned i=0;i<4;++i) {
            glBindTexture(GL_TEXTURE_2D,color[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,32,32,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,color[i],0);
        }
        glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,32,32);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        const GLenum targets[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};glDrawBuffers(4,targets);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"G-buffer fixture incomplete");
    }
    ~Target() {glDeleteFramebuffers(1,&framebuffer);glDeleteRenderbuffers(1,&depth);glDeleteTextures(4,color.data());}
    void bind() {glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,32,32);glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
};
}

int main(int argc,char** argv) {
    try {
        std::cout << std::unitbuf;
        require(argc==2,"Usage: check_follow <production-shader-directory>");Context context;
        auto* pc=const_cast<ptrClass*>(Ge::Engine::getPtrClassAddr());const ptrClass old=*pc;
        GraphicsDataMisc data{};data.str_width=320;data.str_height=240;
        Ge::GraphiquePipelineManager pipelines{};
        Ge::LightManager lights{};Ge::CameraManager cameras{};
        pc->gdm=&data;pc->graphiquePipelineManager=&pipelines;pc->lightManager=&lights;pc->cameraManager=&cameras;
        require(lights.initialize(&data) && cameras.initialize(&data),"Manager initialization failed");
        auto* camera=cameras.createCamera("Terrain test camera",-1);
        camera->setPosition(glm::vec3(32,64,32));camera->setEulerAngles(glm::vec3(-70,0,0));camera->setFar(100);
        SceneData scene;ProceduralGeneration terrain(scene,1337);
        ProceduralGeneration::Config config;
        config.maxLod=2;config.viewDistance=160;config.lodDistanceFactor=.65f;
        config.maxChunksPerFrame=4;config.maxInFlight=4;config.vertexBudgetMiB=64;
        config.collisionEnabled=false;config.field.mode=ProceduralTerrain::FieldMode::Plane;config.field.baseHeight=.25f;
        config.shaderDirectory=argv[1];config.microDetailStrength=0;
        require(terrain.initialize(config),terrain.getLastError());
        settle(terrain);
        require(camera->getFar()==416,"prepareRenderer did not extend the active camera far plane");
        require(terrain.getStats().visibleChunks>0,"Initial coverage not published");
        require(terrain.getStats().selectedChunks==expectedSelection(camera->getPosition(),config),"Initial camera selection mismatch");
        const auto initial=terrain.getStats();
        unsigned selectionsDuringRebuild=0;
        for(unsigned step=1;step<=10;++step) {
            const auto before=terrain.getStats();
            camera->setPosition(glm::vec3(32+40*step,64,32));
            terrain.prepareRenderer();glFinish();
            if(before.rebuilding && terrain.getStats().lodSelections>before.lodSelections)++selectionsDuringRebuild;
            for(unsigned frame=0;frame<3;++frame) {terrain.prepareRenderer();glFinish();}
            require(!terrain.getStats().resourceLimited,"Moving terrain reached a budget failure");
            require(terrain.getStats().visibleChunks>0,"Movement dropped the complete visible coverage");
        }
        require(terrain.getStats().lodSelections>=initial.lodSelections+10,"Camera motion did not replan without Behaviour updates");
        require(selectionsDuringRebuild>0,"Fixture did not replan while GPU work was pending");
        require(terrain.getStats().publishedLodGroups>initial.publishedLodGroups,"No LOD group was published during continuous camera movement");
        settle(terrain);
        require(terrain.getStats().publishedLodGroups>initial.publishedLodGroups,"Moving camera never published new LOD groups");
        require(terrain.getStats().selectedChunks==expectedSelection(camera->getPosition(),config),"Moving camera did not converge to current LOD selection");
        std::cout << "PASS preparation: camera-only movement replans during pending jobs and converges without Behaviour updates\n";

        auto* second=cameras.createCamera("Replacement camera",-2);second->setFar(120);
        second->setPosition(glm::vec3(-180,64,-140));second->setEulerAngles(glm::vec3(-70,0,0));
        settle(terrain);require(second->getFar()==416,"New active camera did not get terrain projection range");
        require(terrain.getStats().selectedChunks==expectedSelection(second->getPosition(),config),"Active camera switch was not followed");
        Target target;target.bind();glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,data.str_ssbo.str_camera);
        terrain.drawRenderer();
        const auto down=terrain.getStats();
        require(down.cameraDrawChunks>0 && down.cameraCulledChunks>0,"Main camera did not filter terrain chunks");
        require(down.cameraDrawChunks+down.cameraCulledChunks==down.visibleChunks,"Main-camera culling accounting mismatch");
        terrain.prepareRenderer();
        // Coarse chunk AABBs extend above the actual plane. Move the camera
        // above every resident box before looking upward; otherwise retaining
        // a coarse box containing the camera is correct conservative culling.
        // Deliberately isolate draw-time culling from another streaming update.
        second->setPosition(glm::vec3(second->getPosition().x,512,second->getPosition().z));
        second->setEulerAngles(glm::vec3(70,0,0));target.bind();glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,data.str_ssbo.str_camera);terrain.drawRenderer();
        require(terrain.getStats().cameraDrawChunks==0,"Sky-facing camera above the resident boxes still submitted terrain");

        std::cout << "Camera culling checked; preparing shadow fixture\n";
        auto* sun=lights.createDirectionalLight(glm::vec3(-65,20,0),glm::vec3(1),"Terrain test sun");sun->setshadow(true);
        const glm::vec3 centre(second->getPosition().x,0,second->getPosition().z);
        const glm::mat4 lightMatrix=glm::ortho(-64.0f,64.0f,-64.0f,64.0f,1.0f,256.0f)*
            glm::lookAt(centre+glm::vec3(0,128,0),centre,glm::vec3(0,0,-1));
        sun->m_shadowMatrix[0].projview=lightMatrix;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,data.str_ssbo.str_shadow);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(ShadowMatrix),&sun->m_shadowMatrix[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,data.str_ssbo.str_shadow);
        glBindFramebuffer(GL_FRAMEBUFFER,lights.getFrameShadowBuffer()[0]);glViewport(0,0,32,32);glClear(GL_DEPTH_BUFFER_BIT);
        terrain.drawShadowRenderer(0);glFinish();
        const auto shadow=terrain.getStats();
        require(shadow.shadowDrawChunks>0 && shadow.shadowCulledChunks>0,"Shadow cascade did not use its independent frustum");
        require(shadow.shadowDrawChunks+shadow.shadowCulledChunks==shadow.visibleChunks,"Shadow culling accounting mismatch");
        float depths[32*32];glReadPixels(0,0,32,32,GL_DEPTH_COMPONENT,GL_FLOAT,depths);
        require(*std::min_element(std::begin(depths),std::end(depths))<1.0f,"Off-camera terrain did not write shadow depth");
        require(glGetError()==GL_NO_ERROR,"GL error in camera/shadow rendering");
        std::cout << "PASS culling: camera rotation removes ground; independent light frustum retains off-camera shadow casters\n";
        shadowAttachments(lights,4);
        auto* point=lights.createPointLight(centre+glm::vec3(0,10,0),glm::vec3(1),"Point fixture");
        point->setshadow(true);shadowAttachments(lights,10);
        point->setshadow(false);shadowAttachments(lights,4);
        sun->setshadow(false);shadowAttachments(lights,0);
        sun->setshadow(true);shadowAttachments(lights,4);
        lights.destroyLight(point);
        auto* spot=lights.createSpotLight(centre+glm::vec3(0,10,0),glm::vec3(1),glm::vec3(-90,0,0),45,"Spot fixture");
        spot->setshadow(true);shadowAttachments(lights,8);
        lights.destroyLight(spot);shadowAttachments(lights,4);
        std::cout << "PASS lights: real directional/point/spot allocation, toggling, framebuffer reattachment and destruction\n";
        std::cout << "Shadows checked; shutting terrain down\n";
        terrain.shutdown();
        require(camera->getFar()==100 && second->getFar()==120,"Shutdown did not restore camera projection ranges");
        require(pipelines.getCustomRenderer().empty(),"Terrain renderer remained registered after shutdown");
        lights.release();cameras.release();*pc=old;
        std::cout << "PASS lifecycle: camera replacement, projection restoration and renderer unregistration\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << "FAIL " << error.what() << '\n';return 1;}
}
