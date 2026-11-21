#include "glcore.hpp"
#include "Camera.hpp"
#include "CameraManager.hpp"
#include "ShaderDataMisc.hpp"
#include "GraphiquePipeline.hpp"
#include "ShaderPair.hpp"
#include "TextureBindings.hpp"
#include "GraphicsDataMisc.hpp"
#include "Engine.hpp"
#include "Time.hpp"
#include "LightManager.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
unsigned queries=0, replacements=0, subWrites=0, bulkBinds=0, activeCalls=0;
PFNGLGETUNIFORMLOCATIONPROC getLocation;
PFNGLBUFFERDATAPROC bufferData;
PFNGLBUFFERSUBDATAPROC bufferSubData;
PFNGLBINDTEXTURESPROC bindTextures;
PFNGLACTIVETEXTUREPROC activeTexture;
GLint GLAPIENTRY countedLocation(GLuint program,const GLchar* name)
{ ++queries; return getLocation(program,name); }
void GLAPIENTRY countedData(GLenum target,GLsizeiptr size,const void* data,GLenum usage)
{ ++replacements; bufferData(target,size,data,usage); }
void GLAPIENTRY countedSubData(GLenum target,GLintptr offset,GLsizeiptr size,const void* data)
{ ++subWrites; bufferSubData(target,offset,size,data); }
void GLAPIENTRY countedBulk(GLuint first,GLsizei count,const GLuint* names)
{ ++bulkBinds; bindTextures(first,count,names); }
void GLAPIENTRY countedActive(GLenum unit)
{ ++activeCalls; activeTexture(unit); }

GLuint compile(GLenum type,const char* text)
{
    GLuint shader=glCreateShader(type); glShaderSource(shader,1,&text,nullptr); glCompileShader(shader);
    GLint valid=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&valid);
    if(!valid) { char log[2048]{}; glGetShaderInfoLog(shader,sizeof(log),nullptr,log); throw std::runtime_error(log); }
    return shader;
}
void link(GLuint program,const std::vector<std::pair<GLenum,const char*>>& sources)
{
    std::vector<GLuint> shaders;
    for(const auto& source:sources) { shaders.push_back(compile(source.first,source.second)); glAttachShader(program,shaders.back()); }
    glLinkProgram(program); GLint valid=0; glGetProgramiv(program,GL_LINK_STATUS,&valid);
    for(auto shader:shaders) { glDetachShader(program,shader); glDeleteShader(shader); }
    require(valid!=0,"fixture program failed to link");
}
constexpr const char* vertex=R"(#version 450 core
uniform int offsetUbo; uniform int offsetShadow;
void main(){ gl_Position=vec4(float(offsetUbo),float(offsetShadow),0,1); })";
constexpr const char* fragment=R"(#version 450 core
out vec4 color; void main(){ color=vec4(1); })";
}

namespace Ge
{
// Services are omitted; the methods being checked are the production classes.
class RenderingEngine
{
public:
    static void initialize(CameraManager& manager,ShaderDataMisc& misc,GraphicsDataMisc& data)
    { require(manager.initialize(&data),"camera initialize"); require(misc.initialize(&data),"misc initialize"); }
    static void update(ShaderDataMisc& misc,Camera* camera) { misc.update(camera); }
    static void release(CameraManager& manager,ShaderDataMisc& misc) { misc.release(); manager.release(); }
};
ShaderPair::ShaderPair() {}
unsigned buildShaderProgram(const std::vector<ShaderFile>&,std::string&,const std::string&,const ShaderCacheOptions&)
{
    GLuint program=glCreateProgram(); link(program,{{GL_VERTEX_SHADER,vertex},{GL_FRAGMENT_SHADER,fragment}}); return program;
}
}

extern "C" __attribute__((visibility("default"))) int frame_upload_tests()
{
    glewExperimental=GL_TRUE;
    const auto status=glewInit();
    if(status!=GLEW_OK && status!=GLEW_ERROR_NO_GLX_DISPLAY)
    { std::cerr<<"GLEW initialization failed\n"; return 1; }
    while(glGetError()!=GL_NO_ERROR) {}
    getLocation=__glewGetUniformLocation; bufferData=__glewBufferData; bufferSubData=__glewBufferSubData;
    bindTextures=__glewBindTextures; activeTexture=__glewActiveTexture;
    __glewGetUniformLocation=countedLocation; __glewBufferData=countedData; __glewBufferSubData=countedSubData;
    __glewBindTextures=countedBulk; __glewActiveTexture=countedActive;
    int result=0;
    try
    {
        Ge::ShaderPair pair;
        Ge::GraphiquePipeline pipeline(nullptr,&pair,""); require(pipeline.isValid(),"pipeline creation");
        const auto first=pipeline.getObjectOffsetLocation(); const auto second=pipeline.getShadowOffsetLocation();
        require(first>=0 && second>=0 && queries==2,"first draw resolves both offsets once");
        for(unsigned frame=0;frame<1000;++frame)
        { require(pipeline.getObjectOffsetLocation()==first,"object cache"); require(pipeline.getShadowOffsetLocation()==second,"shadow cache"); }
        require(queries==2,"steady draws must not query uniforms again");
        constexpr const char* relinked=R"(#version 450 core
layout(location=7) uniform int offsetUbo; layout(location=11) uniform int offsetShadow;
void main(){ gl_Position=vec4(float(offsetUbo),float(offsetShadow),0,1); })";
        link(pipeline.getProgram(),{{GL_VERTEX_SHADER,relinked},{GL_FRAGMENT_SHADER,fragment}});
        pipeline.invalidateShadowContent();
        require(pipeline.getObjectOffsetLocation()==7 && pipeline.getShadowOffsetLocation()==11 && queries==4,
                "explicit in-place relink invalidates uniform locations");

        Ge::Time time; Ge::SettingManager settings; Ge::LightManager lights; GraphicsDataMisc data;
        Ge::Engine::pointers={&time,&settings,&lights};
        Ge::CameraManager cameras; Ge::ShaderDataMisc misc;
        Ge::RenderingEngine::initialize(cameras,misc,data);
        auto* camera=cameras.getCurrentCamera(); const auto cameraName=data.str_ssbo.str_camera;
        GLuint probe=glCreateProgram();
        constexpr const char* compute=R"(#version 450 core
layout(local_size_x=1) in;
layout(std430,binding=0) readonly buffer Camera { vec3 camPosition; mat4 view; mat4 invView; mat4 projection; };
layout(std430,binding=1) readonly buffer Misc { int lights; float time; float gamma; float ambient; float fov; bool ortho; };
layout(std430,binding=2) writeonly buffer Result { uvec4 result[]; };
uniform uint sampleIndex;
void main(){ result[sampleIndex]=uvec4(floatBitsToUint(camPosition.x),floatBitsToUint(time),ortho?1u:0u,floatBitsToUint(projection[0][0])); })";
        link(probe,{{GL_COMPUTE_SHADER,compute}});
        constexpr unsigned count=64; GLuint output=0;
        glCreateBuffers(1,&output); glNamedBufferData(output,count*16,nullptr,GL_STREAM_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,cameraName);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,data.str_ssbo.str_misc);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,output);
        glUseProgram(probe); const auto index=getLocation(probe,"sampleIndex");
        std::array<std::array<unsigned,4>,count> expected{},actual{};
        const auto bits=[](float value) { unsigned result; std::memcpy(&result,&value,4); return result; };
        replacements=subWrites=0;
        for(unsigned i=0;i<count;++i)
        {
            const float x=float(i)*17.25f; time.value=float(i)*0.125f;
            camera->setPosition({x,3,-7}); camera->setOrtho((i&1)!=0);
            Ge::RenderingEngine::update(misc,camera);
            glProgramUniform1ui(probe,index,i); glDispatchCompute(1,1,1);
            expected[i]={bits(x),bits(time.value),i&1,bits(camera->getProjectionMatrix()[0][0])};
        }
        require(replacements==count*3 && subWrites==0,"complete frame payloads replace storage instead of updating queued reads in place");
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT); glGetNamedBufferSubData(output,0,sizeof(actual),actual.data());
        require(actual==expected,"queued readers must retain all64 distinct camera/clock/orthographic payloads");
        replacements=0;
        auto* other=cameras.createCamera("second",-10);
        require(cameras.getCurrentCamera()==other && data.str_ssbo.str_camera==cameraName && replacements==1,
                "camera switch replaces shared payload once without changing buffer identity");
        cameras.releaseCamera(other);
        require(cameras.getCurrentCamera()==camera && replacements==2,"switch back restores prior camera payload once");
        Ge::RenderingEngine::release(cameras,misc);
        glDeleteBuffers(1,&output); glDeleteProgram(probe);

        GLuint textures[5]{},array=0; glCreateTextures(GL_TEXTURE_2D,5,textures); glCreateTextures(GL_TEXTURE_2D_ARRAY,1,&array);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D_ARRAY,array);
        bulkBinds=activeCalls=0; Ge::bindTexture2DSet(0,5,textures);
        GLint active=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        require(bulkBinds==1 && activeCalls==1 && active==GL_TEXTURE4,"five texture pairs become one bulk bind plus legacy active unit");
        for(unsigned i=0;i<5;++i) { GLint bound=0; glGetIntegeri_v(GL_TEXTURE_BINDING_2D,i,&bound); require(unsigned(bound)==textures[i],"material texture binding"); }
        GLint retained=0; glGetIntegeri_v(GL_TEXTURE_BINDING_2D_ARRAY,2,&retained); require(unsigned(retained)==array,"bulk binding preserves other targets");
        GLuint withMissing[5]={textures[0],textures[1],0,textures[3],textures[4]};
        bulkBinds=activeCalls=0; Ge::bindTexture2DSet(0,5,withMissing);
        glGetIntegeri_v(GL_TEXTURE_BINDING_2D_ARRAY,2,&retained);
        require(bulkBinds==0 && activeCalls==5 && unsigned(retained)==array,"zero-ID fallback preserves array target like legacy 2D binds");
        glDeleteTextures(5,textures); glDeleteTextures(1,&array);
        require(glGetError()==GL_NO_ERROR,"unexpected GL error");
        std::cout<<"PASS production frame uploads:64 queued payloads, camera switches,32-bit bool;1000 cached draw pairs/relink;5-texture bindings preserve state\n";
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
    __glewGetUniformLocation=getLocation; __glewBufferData=bufferData; __glewBufferSubData=bufferSubData;
    __glewBindTextures=bindTextures; __glewActiveTexture=activeTexture;
    return result;
}
