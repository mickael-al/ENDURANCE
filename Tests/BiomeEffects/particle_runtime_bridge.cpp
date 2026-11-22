// Production particle adapter + ParticleSystem + allocator + shader compiler.
// Engine entry points are fixtures; GPU work and resource ownership are real.
#include "glcore.hpp"
#include "EnduranceBiomeParticles.hpp"
#include "ParticleSystem.hpp"
#include "Engine.hpp"
#include "RenderingEngine.hpp"
#include "GraphicsDataMisc.hpp"
#include "GraphiquePipeline.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
using namespace EnduranceEnvironment;
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void checkGL(const char* stage) {
    const auto error=glGetError();
    if(error) throw std::runtime_error(std::string(stage)+" GL error "+std::to_string(error));
}

struct Calls {
    unsigned dispatch=0,draw=0,program=0,buffer=0,data=0,subdata=0;
};
struct Init { GLuint buffer=0,offset=0,count=0; } lastInit;
Calls calls;
std::set<GLuint> ownedBuffers,ownedPrograms;
GLuint drawData=0,drawInstances=0;
PFNGLDISPATCHCOMPUTEPROC dispatchOriginal;
PFNGLDRAWARRAYSINSTANCEDPROC drawOriginal;
PFNGLCREATEPROGRAMPROC createProgramOriginal;
PFNGLDELETEPROGRAMPROC deleteProgramOriginal;
PFNGLGENBUFFERSPROC genBuffersOriginal;
PFNGLDELETEBUFFERSPROC deleteBuffersOriginal;
PFNGLBUFFERDATAPROC bufferDataOriginal;
PFNGLBUFFERSUBDATAPROC bufferSubDataOriginal;
void GLAPIENTRY dispatchHook(GLuint x,GLuint y,GLuint z) {
    ++calls.dispatch;
    GLint program=0; glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    auto location=glGetUniformLocation(program,"u_offset");
    if(location>=0) {
        GLint buffer=0; glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&buffer);
        lastInit.buffer=GLuint(buffer);
        glGetUniformuiv(program,location,&lastInit.offset);
        glGetUniformuiv(program,glGetUniformLocation(program,"u_count"),&lastInit.count);
    }
    dispatchOriginal(x,y,z);
}
void GLAPIENTRY drawHook(GLenum mode,GLint first,GLsizei count,GLsizei instances) {
    ++calls.draw;
    GLint buffer=0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,6,&buffer); drawData=GLuint(buffer);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,7,&buffer); drawInstances=GLuint(buffer);
    drawOriginal(mode,first,count,instances);
}
GLuint GLAPIENTRY createProgramHook() {
    ++calls.program; const auto id=createProgramOriginal(); ownedPrograms.insert(id); return id;
}
void GLAPIENTRY deleteProgramHook(GLuint id) { ownedPrograms.erase(id); deleteProgramOriginal(id); }
void GLAPIENTRY genBuffersHook(GLsizei n,GLuint* ids) {
    calls.buffer+=unsigned(n); genBuffersOriginal(n,ids); for(int i=0;i<n;++i) ownedBuffers.insert(ids[i]);
}
void GLAPIENTRY deleteBuffersHook(GLsizei n,const GLuint* ids) {
    for(int i=0;i<n;++i) ownedBuffers.erase(ids[i]); deleteBuffersOriginal(n,ids);
}
void GLAPIENTRY bufferDataHook(GLenum target,GLsizeiptr size,const void* data,GLenum usage) {
    ++calls.data; bufferDataOriginal(target,size,data,usage);
}
void GLAPIENTRY bufferSubDataHook(GLenum target,GLintptr offset,GLsizeiptr size,const void* data) {
    ++calls.subdata; require(data!=nullptr || size==0,"Allocator uploaded a null data pointer");
    bufferSubDataOriginal(target,offset,size,data);
}
struct Hooks {
    Hooks() {
#define HOOK(name,lower) lower##Original=__glew##name; __glew##name=lower##Hook
        HOOK(DispatchCompute,dispatch); HOOK(DrawArraysInstanced,draw);
        HOOK(CreateProgram,createProgram); HOOK(DeleteProgram,deleteProgram);
        HOOK(GenBuffers,genBuffers); HOOK(DeleteBuffers,deleteBuffers);
        HOOK(BufferData,bufferData); HOOK(BufferSubData,bufferSubData);
#undef HOOK
    }
    ~Hooks() {
#define RESTORE(name,lower) __glew##name=lower##Original
        RESTORE(DispatchCompute,dispatch); RESTORE(DrawArraysInstanced,draw);
        RESTORE(CreateProgram,createProgram); RESTORE(DeleteProgram,deleteProgram);
        RESTORE(GenBuffers,genBuffers); RESTORE(DeleteBuffers,deleteBuffers);
        RESTORE(BufferData,bufferData); RESTORE(BufferSubData,bufferSubData);
#undef RESTORE
    }
};

std::vector<GLint64> snapshot() {
    std::vector<GLint64> result;
    for(auto name: {GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ACTIVE_TEXTURE,
                   GL_SHADER_STORAGE_BUFFER_BINDING,GL_UNIFORM_BUFFER_BINDING,
                   GL_COPY_READ_BUFFER_BINDING,GL_COPY_WRITE_BUFFER_BINDING,
                   GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,
                   GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_CULL_FACE_MODE}) {
        GLint value=0; glGetIntegerv(name,&value); result.push_back(value);
    }
    for(auto capability: {GL_CULL_FACE,GL_BLEND,GL_DEPTH_TEST}) result.push_back(glIsEnabled(capability));
    for(unsigned slot: {0u,3u,6u,7u,8u}) {
        GLint value=0; GLint64 start=0,size=0;
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,slot,&value);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,slot,&start);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,slot,&size);
        result.insert(result.end(),{value,start,size});
    }
    GLint value=0; GLint64 start=0,size=0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,6,&value);
    glGetInteger64i_v(GL_UNIFORM_BUFFER_START,6,&start);
    glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE,6,&size);
    result.insert(result.end(),{value,start,size});
    GLint active=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(unsigned unit: {0u,10u,12u,13u}) {
        glActiveTexture(GL_TEXTURE0+unit);
        for(auto target: {GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_3D}) {
            glGetIntegerv(target,&value); result.push_back(value);
        }
        glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&value); result.push_back(value);
    }
    glActiveTexture(active);
    return result;
}

struct Fixture {
    GraphicsDataMisc data{};
    Ge::Time time;
    Ge::Textures white;
    Ge::GraphiquePipelineManager pipelines;
    Ge::RenderingEngine renderer;
    ptrClass pointers;
    std::array<GLuint,5> buffers{};
    std::array<GLuint,4> textures{};
    GLuint sampler=0,vao=0,fbo=0,program=0;
    Fixture() {
        pointers={&pipelines,&renderer,&data,&time};
        Ge::Engine::pointers=&pointers;
        Ge::RenderingEngine::attach(pipelines,&data);
        data.str_width=data.str_height=32;
        glGenBuffers(buffers.size(),buffers.data());
        std::array<float,1024> zeros{};
        for(auto buffer:buffers) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof zeros,zeros.data(),GL_DYNAMIC_DRAW);
        }
        // Camera block begins at a nonzero offset, so base-only restoration fails.
        std::array<float,52> camera{};
        for(unsigned start: {4u,20u,36u}) for(unsigned i=0;i<4;++i) camera[start+i*5]=1;
        camera[36]=camera[41]=1.3f; camera[46]=-1.002f; camera[47]=-1; camera[50]=-.2f;camera[51]=0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,1024,sizeof camera,camera.data());
        data.str_work_id=buffers[4];
        glGenTextures(textures.size(),textures.data());
        for(unsigned i=0;i<textures.size();++i) {
            const auto target=i==3?GL_TEXTURE_3D:GL_TEXTURE_2D;
            glBindTexture(target,textures[i]);
            std::vector<float> pixels(32*32*4,1.f);
            if(i==1) for(unsigned n=0;n<pixels.size();n+=4) {pixels[n]=-10000;pixels[n+1]=0;}
            if(i==3) glTexImage3D(target,0,GL_RGBA32F,1,1,1,0,GL_RGBA,GL_FLOAT,pixels.data());
            else glTexImage2D(target,0,GL_RGBA32F,32,32,0,GL_RGBA,GL_FLOAT,pixels.data());
            glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        white.id=textures[0]; data.str_default_texture=&white;
        renderer.framebuffer.position=textures[1];
        renderer.atmosphere.buffer=buffers[4]; renderer.atmosphere.volume=textures[3];
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[2],0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Fixture framebuffer");
        glViewport(0,0,32,32);glDisable(GL_DEPTH_TEST);
        glGenVertexArrays(1,&vao);glGenSamplers(1,&sampler);
        glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        const char* source="#version 450 core\nlayout(local_size_x=1) in;void main(){}";
        auto shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
        program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glDeleteShader(shader);
        bindSentinels();checkGL("fixture");
    }
    void bindSentinels() {
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,buffers[0],1024,208);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER,3,buffers[1],1024,64);
        for(unsigned slot: {6u,7u,8u}) glBindBufferRange(GL_SHADER_STORAGE_BUFFER,slot,buffers[2],1024,128);
        glBindBufferRange(GL_UNIFORM_BUFFER,6,buffers[3],1024,512);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[4]);glBindBuffer(GL_UNIFORM_BUFFER,buffers[4]);
        glBindBuffer(GL_COPY_READ_BUFFER,buffers[0]);glBindBuffer(GL_COPY_WRITE_BUFFER,buffers[1]);
        for(unsigned unit: {0u,10u,12u,13u}) {
            glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,textures[0]);
            glBindTexture(GL_TEXTURE_3D,textures[3]);glBindSampler(unit,sampler);
        }
        glActiveTexture(GL_TEXTURE4);glBindVertexArray(vao);glUseProgram(program);
        glEnable(GL_CULL_FACE);glDepthMask(GL_TRUE);
    }
    ~Fixture() {
        glUseProgram(0);glDeleteProgram(program);glDeleteSamplers(1,&sampler);
        glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);
        glDeleteTextures(textures.size(),textures.data());glDeleteBuffers(buffers.size(),buffers.data());
        Ge::Engine::pointers=nullptr;
    }
};

std::vector<unsigned char> particles(const Init& allocation) {
    std::vector<unsigned char> bytes(allocation.count*sizeof(Ge::Particle));
    glGetNamedBufferSubData(allocation.buffer,allocation.offset*4,bytes.size(),bytes.data());
    return bytes;
}
void run() {
    Fixture f;
    Hooks hooks;
    EnduranceBiomeParticles adapter;
    require(!adapter.initialize(nullptr),"Null engine accepted");
    require(adapter.initialize(&f.pointers),adapter.error().c_str());
    require(f.pipelines.GetPipelines().size()==9,"Expected eight precompiled families and engine base program");
    require(calls.program==12,"Expected eight families, base sprite, three compute programs");
    const unsigned programs=calls.program;
    require(f.renderer.callbacks.empty(),"Empty adapter registered a render callback");
    std::array<float,EffectCount> strengths{};
    auto update=[&](glm::vec3 position=glm::vec3(0),bool teleport=false,float density=1.f) {
        const auto before=snapshot();
        adapter.update(strengths,position,{0,0,-1},{3,1},1.f/60,teleport,density);
        require(snapshot()==before,"Update leaked GL bindings/program or indexed buffer ranges");
        checkGL("update");
    };
    auto render=[&]() {
        const auto before=snapshot();bool depthWrite=true;
        f.renderer.render(&depthWrite);
        require(snapshot()==before,"Render leaked GL state or indexed buffer ranges");
        if(adapter.groupCount()) require(!depthWrite,"Shared depth-write protocol not updated");
        checkGL("render");
    };
    f.bindSentinels();const auto empty=calls;
    for(int i=0;i<10;++i) { update({float(i),0,0});render(); }
    require(calls.dispatch==empty.dispatch && calls.draw==empty.draw && calls.data==empty.data
        && calls.subdata==empty.subdata && f.renderer.calls==0,"Dormant effects did GPU/callback work");
    std::cout<<"PASS native dormant adapter: zero dispatch, draw, uploads and callbacks\n";

    const auto id=std::size_t(EffectId::WindPollen);
    strengths[id]=1;update();
    require(adapter.groupCount()==1 && adapter.liveParticleCount()==effectDefinitions()[id].particles.count,"Group allocation mismatch");
    require(f.renderer.callbacks.size()==1,"Active adapter needs exactly one callback");
    const Init allocation=lastInit;
    const auto initialParticles=particles(allocation);
    render();
    const auto emittedParticles=particles(allocation);
    require(emittedParticles!=initialParticles,"Production simulation did not advance particles");
    const auto storage=ownedBuffers;const auto stable=calls;const auto dataId=drawData,instancesId=drawInstances;
    for(int i=0;i<60;++i) {
        strengths[id]=1.f-float(i+1)/100;
        update({float(i+1),3,-2});
    }
    require(particles(allocation)==emittedParticles,"Changing camera/fade recreated or moved emitted particles");
    require(ownedBuffers==storage && calls.buffer==stable.buffer && calls.data==stable.data
        && calls.dispatch==stable.dispatch,"Camera/fade reallocates or dispatches initialization");
    render();require(drawData==dataId && drawInstances==instancesId,"Stable groups replaced packed buffers");
    const auto beforeTeleport=calls.dispatch;
    update({5000,20,4000},true);
    require(calls.dispatch==beforeTeleport+1,"Teleport did not restart existing particles once");
    require(particles(allocation)==initialParticles,"Teleport retained old positions/lifetimes");
    const auto densityBefore=calls.dispatch;
    update({},false,2);
    require(adapter.liveParticleCount()==2*effectDefinitions()[id].particles.count
        && calls.dispatch==densityBefore+1,"Changing density must resize and initialize once");
    const auto densityStable=calls;
    update({2,0,0},false,2);
    require(calls.dispatch==densityStable.dispatch && calls.buffer==densityStable.buffer
        && calls.data==densityStable.data,"Stable density reallocates or initializes every frame");
    update({},false,0);
    const auto zeroDensity=calls;
    for(unsigned i=0;i<5;++i) { update({},false,0);render(); }
    require(adapter.groupCount()==0 && f.renderer.callbacks.empty() && calls.dispatch==zeroDensity.dispatch
        && calls.draw==zeroDensity.draw && calls.subdata==zeroDensity.subdata,"Zero density does GPU work");
    std::cout<<"PASS native moving/fading group: stable buffers/counts, world-space particles, teleport restart\n";

    strengths={};update();
    require(adapter.groupCount()==0 && adapter.liveParticleCount()==0 && f.renderer.callbacks.empty(),"Zero strength did not release groups/callback");
    for(std::size_t i=0;i<EffectCount;++i) {
        strengths={};strengths[i]=1;update();
        const auto count=effectDefinitions()[i].particles.count;
        require(adapter.groupCount()==(count?1u:0u) && adapter.liveParticleCount()==count,"Recipe transition retained stale groups");
        render();require(calls.program==programs,"Transition compiled another shader program");
    }
    strengths.fill(1);update();require(adapter.groupCount()==8,"Eight-group hard cap not enforced");
    require(adapter.liveParticleCount()<=32768,"Particle budget exceeded");
    strengths={};update();
    for(auto event: {EffectId::Rain,EffectId::HeavyRain,EffectId::Sandstorm,EffectId::Blizzard}) strengths[std::size_t(event)]=1;
    update({},false,4);require(adapter.liveParticleCount()==32768 && adapter.groupCount()<=4,
        "Particle budget must truncate last eligible group at 32768");
    const auto truncated=calls;
    update({1,0,0},false,40);
    require(adapter.liveParticleCount()==32768 && calls.dispatch==truncated.dispatch
        && calls.buffer==truncated.buffer,"Clamped density recreated budget-truncated groups");
    strengths={};update();
    strengths[std::size_t(EffectId::Rain)]=strengths[std::size_t(EffectId::HeavyRain)]=1;
    update({},false,4);
    require(adapter.liveParticleCount()==32768 && adapter.groupCount()==2,"Promotion fixture did not saturate pool");
    const auto beforePromotion=calls.dispatch;
    strengths[std::size_t(EffectId::Rain)]=0;update({},false,4);
    require(adapter.groupCount()==1 && adapter.liveParticleCount()==4*effectDefinitions()[std::size_t(EffectId::HeavyRain)].particles.count
        && calls.dispatch==beforePromotion+1,"Truncated group did not recover density when budget became available");
    const auto promoted=calls;update({},false,4);
    require(calls.dispatch==promoted.dispatch && calls.subdata==promoted.subdata && calls.data==promoted.data
        && calls.buffer==promoted.buffer,"Promoted unchanged group rebuilt/uploaded again");
    update({},false,std::numeric_limits<float>::quiet_NaN());
    require(adapter.liveParticleCount()==effectDefinitions()[std::size_t(EffectId::HeavyRain)].particles.count,
        "Nonfinite density did not fall back to configured default");
    strengths.fill(std::numeric_limits<float>::quiet_NaN());update();
    require(adapter.groupCount()==0 && f.renderer.callbacks.empty(),"Nonfinite strengths retained live groups");
    strengths[id]=1;update();update({std::numeric_limits<float>::infinity(),0,0});
    require(adapter.groupCount()==0 && f.renderer.callbacks.empty(),"Invalid camera retained live groups");
    std::cout<<"PASS all40 native transitions: precompiled programs, density0..4, eight groups/32768 slots, invalid input cleanup\n";

    strengths={};strengths[id]=1;update();
    const auto resources=ownedBuffers;const auto shaders=ownedPrograms;
    adapter.release();adapter.release();
    require(ownedBuffers.empty() && ownedPrograms.empty() && f.pipelines.GetPipelines().empty()
        && f.renderer.callbacks.empty(),"Release leaked resources, pipelines or callback");
    for(auto id:resources) require(!glIsBuffer(id),"Released buffer still alive");
    // GL may defer program destruction while submitted draws hold references.
    // Require an actual delete request and no current binding, not synchronous
    // driver reclamation (the renderer must never glFinish to release resources).
    for(auto id:shaders) if(glIsProgram(id)) {
        GLint current=0,deleted=0;glGetIntegerv(GL_CURRENT_PROGRAM,&current);glGetProgramiv(id,GL_DELETE_STATUS,&deleted);
        require(deleted && GLuint(current)!=id,"Released program was not deleted or remains current");
    }
    require(adapter.groupCount()==0 && adapter.liveParticleCount()==0,"Release retained group counts");
    require(adapter.initialize(&f.pointers),"Reinitialize failed");
    require(f.renderer.callbacks.empty(),"Reinitialize restored obsolete callback");
    adapter.release();checkGL("release/reinitialize");
    std::cout<<"PASS native GL isolation: range offsets/sizes, textures/samplers, program/VAO/cull; release/reinitialize cleanup\n";
}
}
extern "C" __attribute__((visibility("default"))) int particles_validate(const char* root) {
    const auto previous=std::filesystem::current_path();
    try {
        glewExperimental=GL_TRUE;const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization");
        while(glGetError()!=GL_NO_ERROR) {}
        std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
        run();std::filesystem::current_path(previous);return 1;
    } catch(const std::exception& e) {
        std::filesystem::current_path(previous);std::cerr<<"Native particle test: "<<e.what()<<'\n';return 0;
    }
}
