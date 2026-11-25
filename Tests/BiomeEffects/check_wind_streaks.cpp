// Real wind class, shader compiler, GL allocation and drawing; scene services
// are shared fixtures. GPU reads and call interception exist only in this test.
#include "glcore.hpp"
#include "EnduranceWindStreaks.hpp"
#include "Engine.hpp"
#include "RenderingEngine.hpp"
#include "GraphicsDataMisc.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
void checkGL(const char* stage) {
    const auto error=glGetError();
    if(error) throw std::runtime_error(std::string(stage)+" GL error "+std::to_string(error));
}
std::vector<GLint64> state() {
    std::vector<GLint64> values;
    for(auto name: {GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ACTIVE_TEXTURE,
                   GL_SHADER_STORAGE_BUFFER_BINDING,GL_UNIFORM_BUFFER_BINDING,
                   GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,
                   GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA}) {
        GLint value=0;glGetIntegerv(name,&value);values.push_back(value);
    }
    for(auto capability: {GL_CULL_FACE,GL_BLEND,GL_DEPTH_TEST}) values.push_back(glIsEnabled(capability));
    for(auto target: {GL_SHADER_STORAGE_BUFFER,GL_UNIFORM_BUFFER}) {
        const bool ssbo=target==GL_SHADER_STORAGE_BUFFER;
        for(unsigned index: {0u,6u}) {
            GLint value=0;GLint64 offset=0,size=0;
            glGetIntegeri_v(ssbo?GL_SHADER_STORAGE_BUFFER_BINDING:GL_UNIFORM_BUFFER_BINDING,index,&value);
            glGetInteger64i_v(ssbo?GL_SHADER_STORAGE_BUFFER_START:GL_UNIFORM_BUFFER_START,index,&offset);
            glGetInteger64i_v(ssbo?GL_SHADER_STORAGE_BUFFER_SIZE:GL_UNIFORM_BUFFER_SIZE,index,&size);
            values.insert(values.end(),{value,offset,size});
        }
    }
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(unsigned unit: {0u,10u,12u,13u}) {
        glActiveTexture(GL_TEXTURE0+unit);
        for(auto target: {GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_3D}) {
            GLint value=0;glGetIntegerv(target,&value);values.push_back(value);
        }
        GLint sampler=0;glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&sampler);values.push_back(sampler);
    }
    glActiveTexture(active);return values;
}
struct Fixture {
    static constexpr unsigned Width=256,Height=160;
    GraphicsDataMisc data{};
    Ge::Time time;
    Ge::GraphiquePipelineManager pipelines;
    Ge::RenderingEngine renderer;
    ptrClass pointers;
    std::array<GLuint,4> buffers{};
    std::array<GLuint,3> textures{};
    GLuint sampler=0,vao=0,fbo=0;
    Fixture() {
        pointers={&pipelines,&renderer,&data,&time};Ge::Engine::pointers=&pointers;
        Ge::RenderingEngine::attach(pipelines,&data);
        data.str_width=Width;data.str_height=Height;
        glGenBuffers(buffers.size(),buffers.data());
        std::array<float,1024> zeros{};
        for(auto buffer:buffers) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof zeros,zeros.data(),GL_DYNAMIC_DRAW);
        }
        std::array<float,52> camera{};
        for(unsigned start: {4u,20u,36u}) for(unsigned i=0;i<4;++i) camera[start+i*5]=1;
        camera[36]=.75f*float(Height)/Width;camera[41]=.75f;
        camera[46]=-1.0002f;camera[47]=-1;camera[50]=-.2f;camera[51]=0;
        glNamedBufferSubData(buffers[0],1024,sizeof camera,camera.data());
        glGenTextures(textures.size(),textures.data());
        for(unsigned i=0;i<textures.size();++i) {
            const auto target=i==2?GL_TEXTURE_3D:GL_TEXTURE_2D;
            glBindTexture(target,textures[i]);
            if(i==2) glTexImage3D(target,0,GL_RGBA32F,1,1,1,0,GL_RGBA,GL_FLOAT,zeros.data());
            else glTexImage2D(target,0,GL_RGBA32F,Width,Height,0,GL_RGBA,GL_FLOAT,nullptr);
            glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        opaqueDepth(-10000.f);
        renderer.framebuffer.position=textures[0];
        renderer.atmosphere.buffer=buffers[3];renderer.atmosphere.volume=textures[2];
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Wind fixture framebuffer");
        glViewport(0,0,Width,Height);glDisable(GL_DEPTH_TEST);
        glGenVertexArrays(1,&vao);glGenSamplers(1,&sampler);
        glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        bind();checkGL("fixture");
    }
    void opaqueDepth(float depth) {
        const float value[4]={depth,0,0,0};glClearTexImage(textures[0],0,GL_RGBA,GL_FLOAT,value);
    }
    void bind() {
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,buffers[0],1024,208);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER,6,buffers[1],1024,128);
        glBindBufferRange(GL_UNIFORM_BUFFER,6,buffers[2],1024,512);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[3]);glBindBuffer(GL_UNIFORM_BUFFER,buffers[3]);
        for(unsigned unit: {0u,10u,12u,13u}) {
            glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,textures[0]);
            glBindTexture(GL_TEXTURE_3D,textures[2]);glBindSampler(unit,sampler);
        }
        glActiveTexture(GL_TEXTURE4);glBindVertexArray(vao);glUseProgram(0);
        glEnable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_TRUE);
    }
    ~Fixture() {
        glUseProgram(0);glDeleteSamplers(1,&sampler);glDeleteVertexArrays(1,&vao);
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(textures.size(),textures.data());
        glDeleteBuffers(buffers.size(),buffers.data());Ge::Engine::pointers=nullptr;
    }
};
struct Record { std::array<float,16> words; };
unsigned drawCount=0;
GLuint drawnBuffer=0,drawnProgram=0,drawnVao=0;
glm::vec3 drawnAnchor{};
std::vector<Record> records;
PFNGLDRAWARRAYSINSTANCEDPROC originalDraw;
void GLAPIENTRY drawHook(GLenum mode,GLint first,GLsizei count,GLsizei instances) {
    require(mode==GL_TRIANGLE_STRIP && first==0 && count>=6 && count<=130,"Unbounded/non-ribbon wind draw");
    require(instances>0 && instances<=int(EnduranceWindStreaks::MaxStreaks),"Wind instance budget exceeded");
    ++drawCount;
    GLint buffer=0,program=0,vao=0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,6,&buffer);
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    drawnBuffer=GLuint(buffer);drawnProgram=GLuint(program);drawnVao=GLuint(vao);
    records.resize(instances);
    glGetNamedBufferSubData(drawnBuffer,0,records.size()*sizeof(Record),records.data());
    const GLint anchor=glGetUniformLocation(drawnProgram,"uWindAnchor");
    require(anchor>=0,"Wind has no stable local anchor");
    glGetUniformfv(drawnProgram,anchor,&drawnAnchor.x);
    for(const auto& r:records) for(float v:r.words) require(std::isfinite(v),"Nonfinite wind instance");
    originalDraw(mode,first,count,instances);
}
struct DrawHook {
    DrawHook() { originalDraw=__glewDrawArraysInstanced;__glewDrawArraysInstanced=drawHook; }
    ~DrawHook() { __glewDrawArraysInstanced=originalDraw; }
};
std::vector<glm::dvec3> centers() {
    std::vector<glm::dvec3> result;
    for(const auto& r:records) result.push_back(glm::dvec3(drawnAnchor)+glm::dvec3(r.words[0],r.words[1],r.words[2]));
    return result;
}
double imageEnergy() {
    std::vector<float> pixels(Fixture::Width*Fixture::Height*4);
    glReadPixels(0,0,Fixture::Width,Fixture::Height,GL_RGBA,GL_FLOAT,pixels.data());
    double energy=0;
    for(float v:pixels) { require(std::isfinite(v) && v>=0,"Wind raster produced invalid pixels");energy+=v; }
    return energy;
}
void run() {
    Fixture f;DrawHook hook;EnduranceWindStreaks wind;
    require(!wind.initialize(nullptr),"Wind accepted null engine");
    require(wind.initialize(&f.pointers),wind.error().c_str());
    f.bind();
    require(f.renderer.callbacks.empty() && !wind.liveCount(),"Inactive wind registered a draw");
    auto update=[&](float strength,glm::vec3 camera=glm::vec3(0),glm::vec2 velocity=glm::vec2(12,1),
                    float dt=0,bool teleport=false,float density=1) {
        const auto before=state();wind.update(strength,camera,velocity,dt,teleport,density);
        require(state()==before,"Wind update leaked GL state");checkGL("wind update");
    };
    auto render=[&]() {
        f.bind();glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        const auto before=state();bool depth=true;const auto draws=drawCount;
        f.renderer.render(&depth);
        require(state()==before,"Wind draw leaked bindings/ranges/texture/cull state");
        require(drawCount-draws<2,"Wind used multiple draws for one field");
        if(drawCount!=draws) { GLboolean mask=GL_TRUE;glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
            require(!depth && mask==GL_FALSE,"Wind broke shared transparent depth-write protocol"); }
        checkGL("wind draw");
    };
    for(unsigned i=0;i<5;++i) { update(0);render(); }
    require(!drawCount && f.renderer.callbacks.empty(),"Dormant wind does draw work");
    update(1,{},glm::vec2(12,1),.016f);
    require(wind.liveCount()>1 && wind.liveCount()<=EnduranceWindStreaks::MaxStreaks,
        "Wind field lacks a bounded population");
    require(f.renderer.callbacks.size()==1,"Active wind needs exactly one callback");
    render();require(drawCount>0,"Active wind did not draw");
    const auto born=centers();const auto buffer=drawnBuffer;
    update(1,glm::vec3(.01f,0,0));render();
    const auto movedCamera=centers();unsigned retained=0;
    for(const auto& p:movedCamera) for(const auto& q:born) if(glm::length(p-q)<1e-5) { ++retained;break; }
    require(retained>born.size()*9/10,"Camera movement transported wind streaks");
    update(1,glm::vec3(.01f,0,0),glm::vec2(12,1),.05f);render();
    require(drawnBuffer==buffer,"Wind update replaced the instance buffer");
    const auto advected=centers();double displacement=0;
    for(unsigned i=0;i<(std::min)(advected.size(),movedCamera.size());++i)
        displacement+=glm::length(advected[i]-movedCamera[i]);
    require(displacement>.01,"Wind field does not move with elapsed time");
    for(unsigned i=0;i<30;++i) update(1,{},glm::vec2(12,1),1.f/60);
    render();require(imageEnergy()>0,"Wind strips produce no visible fragments");
    if(const char* path=std::getenv("ENDURANCE_WIND_PREVIEW")) {
        std::vector<float> linear(Fixture::Width*Fixture::Height*3);
        glReadPixels(0,0,Fixture::Width,Fixture::Height,GL_RGB,GL_FLOAT,linear.data());
        // Fixed display gamma only; do not brighten the shader for the preview.
        std::vector<unsigned char> pixels(linear.size());
        for(unsigned i=0;i<pixels.size();++i)
            pixels[i]=static_cast<unsigned char>(255*std::pow(std::clamp(linear[i],0.f,1.f),1.f/2.2f)+.5f);
        std::ofstream image(path,std::ios::binary);
        require(bool(image),"Cannot write wind preview");
        image<<"P6\n"<<Fixture::Width<<" "<<Fixture::Height<<"\n255\n";
        for(unsigned row=Fixture::Height;row--;)
            image.write(reinterpret_cast<const char*>(pixels.data()+row*Fixture::Width*3),Fixture::Width*3);
    }
    f.opaqueDepth(-.01f);render();require(imageEnergy()==0,"Wind strips render through opaque foreground");
    f.opaqueDepth(-10000.f);
    const glm::vec3 destination(40000,120,-40000);
    update(1,destination,glm::vec2(20,0),0,true);render();
    require(!records.empty(),"Teleport removed the whole wind field");
    for(const auto& p:centers()) require(glm::length(p-glm::dvec3(destination))<400,
        "Teleport retained wind near the old camera");
    update(1,destination,glm::vec2(0),.016f);render();
    update(1,destination,glm::vec2(std::numeric_limits<float>::infinity()),
        std::numeric_limits<float>::quiet_NaN());render();
    update(1,destination,{},0,false,0);render();
    require(!wind.liveCount() && f.renderer.callbacks.empty(),"Zero density retains wind callbacks");
    update(1,{},glm::vec2(12,1),.016f,false,4);render();
    require(wind.liveCount()<=EnduranceWindStreaks::MaxStreaks,"Density setting exceeds bounded pool");
    update(1,glm::vec3(std::numeric_limits<float>::infinity()));
    require(!wind.liveCount() && f.renderer.callbacks.empty(),"Invalid camera retains live wind");
    update(1);render();const auto resource=drawnBuffer,program=drawnProgram,vao=drawnVao;
    wind.release();wind.release();
    require(!wind.liveCount() && f.renderer.callbacks.empty() && f.pipelines.GetPipelines().empty(),
        "Wind release retains ownership/callback");
    require(!glIsBuffer(resource) && !glIsVertexArray(vao),"Wind release leaked GL buffers/VAO");
    if(glIsProgram(program)) { GLint deleted=0;glGetProgramiv(program,GL_DELETE_STATUS,&deleted);
        require(deleted,"Wind program was not deleted"); }
    require(wind.initialize(&f.pointers),"Wind reinitialization failed");
    require(f.renderer.callbacks.empty(),"Reinitialize restored old wind callback");
    wind.release();checkGL("wind cleanup");

    // Reinitialize the same seeded field for each trajectory. Compare the
    // actual uploaded geometry, not a second implementation of its phase law.
    struct Trajectory { std::vector<Record> born, moved; };
    auto trajectory=[&](float speed,float dt,unsigned steps) {
        require(wind.initialize(&f.pointers),"Wind trajectory initialization failed");
        f.bind();update(1,{},glm::vec2(speed,0),0,true);render();
        Trajectory result{records,{}};
        for(unsigned i=0;i<steps;++i) update(1,{},glm::vec2(speed,0),dt);
        render();result.moved=records;
        require(result.born.size()==result.moved.size(),"Short trajectory respawned/recounted wind");
        wind.release();return result;
    };
    const auto stationary=trajectory(0,.25f,2);
    for(unsigned i=0;i<stationary.born.size();++i)
        for(unsigned word=0;word<12;++word)
            require(std::abs(stationary.born[i].words[word]-stationary.moved[i].words[word])<1e-6f,
                "Wind geometry or phase drifts while air velocity is zero");
    const auto slow=trajectory(2,.25f,2);       // 1 m in 0.5 s
    const auto fast=trajectory(8,.0625f,2);     // 1 m in 0.125 s
    const auto subdivided=trajectory(2,.0625f,8); // Same 1 m with smaller steps
    const auto farther=trajectory(4,.25f,2);   // Twice the distance in the same time
    require(slow.moved.size()==fast.moved.size() && slow.moved.size()==subdivided.moved.size()
        && slow.moved.size()==farther.moved.size(),"Trajectory populations differ");
    constexpr double Cycle=6.283185307179586;
    for(unsigned i=0;i<slow.moved.size();++i) {
        for(unsigned word=0;word<12;++word) {
            require(std::abs(slow.moved[i].words[word]-fast.moved[i].words[word])<5e-6f,
                "Equal wind travel at different speeds changes ribbon geometry");
            require(std::abs(slow.moved[i].words[word]-subdivided.moved[i].words[word])<5e-6f,
                "Wind phase depends on timestep subdivision");
        }
        const double phase=std::remainder(double(slow.moved[i].words[11])-slow.born[i].words[11],Cycle);
        const double twice=std::remainder(double(farther.moved[i].words[11])-farther.born[i].words[11],Cycle);
        require(std::abs(phase)>.01 && std::abs(twice-2*phase)<5e-6,
            "Doubling traveled distance does not double wind phase advance");
    }
    checkGL("wind travel-linked phase");
    std::cout<<"PASS wind phase: no zero-speed drift, equal travel gives equal phase, "
        "timestep subdivision invariance and proportional speed response\n";
    std::cout<<"PASS wind: zero dormant draws; bounded instanced strips; world anchoring/advection; "
        "teleport and invalid input; opaque occlusion; GL state and cleanup\n";
}
}
extern "C" __attribute__((visibility("default"))) int wind_validate(const char* root) {
    const auto previous=std::filesystem::current_path();
    try {
        glewExperimental=GL_TRUE;const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization");
        while(glGetError()!=GL_NO_ERROR) {}
        std::filesystem::current_path(std::filesystem::path(root)/"ENDURANCE");
        run();std::filesystem::current_path(previous);return 1;
    } catch(const std::exception& e) {
        std::filesystem::current_path(previous);std::cerr<<"Native wind test: "<<e.what()<<'\n';return 0;
    }
}
