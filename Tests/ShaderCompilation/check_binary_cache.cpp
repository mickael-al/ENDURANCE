// Real EGL programs exercise disk reuse and fallback. Entry-point counters
// distinguish binary loads from a hidden source recompile; no timing assertions.
#include "glcore.hpp"
#include "ComputeShader.hpp"
#include "GraphiquePipeline.hpp"
#include "ShaderCompilation.hpp"
#include "ShaderPair.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool fakeRenderer=false, zeroFormats=false;
using GetStringFunction=const GLubyte* (GLAPIENTRY*)(GLenum);
using GetIntegerFunction=void (GLAPIENTRY*)(GLenum,GLint*);
GetStringFunction realGetString=nullptr;
GetIntegerFunction realGetInteger=nullptr;
}
// GL1.0 functions are not GLEW pointers. Bind these fixture-only wrappers locally
// with -Bsymbolic-functions; all ordinary queries still reach the real driver.
extern "C" const GLubyte* GLAPIENTRY glGetString(GLenum name)
{
    if(fakeRenderer && name==GL_RENDERER)
        return reinterpret_cast<const GLubyte*>("binary-cache-test alternate renderer");
    return realGetString(name);
}
extern "C" void GLAPIENTRY glGetIntegerv(GLenum name,GLint* value)
{
    if(zeroFormats && name==GL_NUM_PROGRAM_BINARY_FORMATS) { *value=0; return; }
    realGetInteger(name,value);
}
namespace {
namespace fs=std::filesystem;
void require(bool condition,const std::string& message)
{ if(!condition)throw std::runtime_error(message); }
void write(const fs::path& path,const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary);file.write(text.data(),text.size());
    require(file.good(),"Cannot write fixture "+path.string());
}
std::vector<char> read(const fs::path& path)
{
    std::ifstream file(path,std::ios::binary);
    require(file.good(),"Cannot read cache fixture");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void writeBytes(const fs::path& path,const std::vector<char>& bytes)
{ write(path,std::string(bytes.begin(),bytes.end())); }
std::vector<fs::path> cacheFiles(const fs::path& path)
{
    std::vector<fs::path> files;
    if(fs::is_directory(path))for(const auto& item:fs::directory_iterator(path))
        if(item.is_regular_file() && item.path().extension()==".glbin")files.push_back(item.path());
    return files;
}
struct Counts { unsigned compile=0,link=0,load=0,save=0,deleted=0; } counts;
PFNGLCOMPILESHADERPROC realCompile;
PFNGLLINKPROGRAMPROC realLink;
PFNGLPROGRAMBINARYPROC realBinary;
PFNGLGETPROGRAMBINARYPROC realGetBinary;
PFNGLDELETEPROGRAMPROC realDeleteProgram;
bool rejectNextBinary=false;
void GLAPIENTRY compile(GLuint shader){++counts.compile;realCompile(shader);}
void GLAPIENTRY link(GLuint program){++counts.link;realLink(program);}
void GLAPIENTRY binary(GLuint program,GLenum format,const void* data,GLsizei length)
{
    ++counts.load;
    // A newly created program remains unlinked, simulating driver rejection
    // without passing malformed opaque data to the driver.
    if(rejectNextBinary){rejectNextBinary=false;return;}
    realBinary(program,format,data,length);
}
void GLAPIENTRY getBinary(GLuint p,GLsizei size,GLsizei* length,GLenum* format,void* data)
{++counts.save;realGetBinary(p,size,length,format,data);}
void GLAPIENTRY deleteProgram(GLuint program){++counts.deleted;realDeleteProgram(program);}
struct Hooks
{
    Hooks()
    {
        realCompile=__glewCompileShader;realLink=__glewLinkProgram;realBinary=__glewProgramBinary;
        realGetBinary=__glewGetProgramBinary;realDeleteProgram=__glewDeleteProgram;
        __glewCompileShader=compile;__glewLinkProgram=link;__glewProgramBinary=binary;
        __glewGetProgramBinary=getBinary;__glewDeleteProgram=deleteProgram;
    }
    ~Hooks()
    {
        __glewCompileShader=realCompile;__glewLinkProgram=realLink;__glewProgramBinary=realBinary;
        __glewGetProgramBinary=realGetBinary;__glewDeleteProgram=realDeleteProgram;
        fakeRenderer=zeroFormats=rejectNextBinary=false;
    }
};
void expected(unsigned compileCount,unsigned linkCount,unsigned loadCount,const char* label)
{
    require(counts.compile==compileCount && counts.link==linkCount && counts.load==loadCount,
        std::string(label)+": unexpected compile/link/load "+std::to_string(counts.compile)+"/"+
        std::to_string(counts.link)+"/"+std::to_string(counts.load));
}
std::uint32_t dispatch(GLuint program)
{
    GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    std::uint32_t value=0;glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(value),&value,GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);glUseProgram(program);glDispatchCompute(1,1,1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(value),&value);
    glUseProgram(0);glDeleteBuffers(1,&buffer);return value;
}
void prepareCompute(const fs::path& directory)
{
    write(directory/"nested/value.glsl","uint helper() { return 35u; }\n");
    write(directory/"bridge.glsl","#include \"nested/value.glsl\"\n");
    write(directory/"compute.glsl","#version 450 core\n#include \"bridge.glsl\"\n"
        "layout(local_size_x=1) in;\nlayout(std430,binding=0) buffer Output { uint value; };\n"
        "uniform uint extra;\nvoid main() { value=helper()+DEFINED_VALUE+extra; }\n");
}
constexpr const char* defines="#define DEFINED_VALUE 7u\n";
GLuint makeCompute(const fs::path& directory,const Ge::ShaderCacheOptions& options,
                   unsigned value=42,const char* definitions=defines)
{
    Ge::ComputeShader shader((directory/"compute.glsl").string(),definitions,"cache-test",options);
    require(shader.isValid() && shader.getLastError().empty(),shader.getLastError());
    require(dispatch(shader.getProgram())==value,"Cached compute result is wrong");
    const auto program=shader.takeProgram();
    require(shader.getProgram()==0 && shader.takeProgram()==0,"Cached ownership transfer failed");
    return program;
}
void releaseCompute(const fs::path& directory,const Ge::ShaderCacheOptions& options,
                    unsigned value=42,const char* definitions=defines)
{auto program=makeCompute(directory,options,value,definitions);glDeleteProgram(program);}
void checkComputeCache(const fs::path& directory)
{
    prepareCompute(directory);Ge::ShaderCacheOptions options{true,(directory/"cache").string()};
    counts={};auto cold=makeCompute(directory,options);expected(1,1,0,"Cold compute");
    require(counts.save==1 && cacheFiles(options.directory).size()==1,"Cold compute did not persist a binary");
    glProgramUniform1ui(cold,glGetUniformLocation(cold,"extra"),17);
    require(dispatch(cold)==59,"Nondefault compute uniform was not applied");
    glDeleteProgram(cold);require(!glIsProgram(cold),"Transferred cold program leaked");
    counts={};auto warm=makeCompute(directory,options);expected(0,0,1,"Warm compute");
    require(counts.save==0,"Warm binary unnecessarily saved again");
    glProgramUniform1ui(warm,glGetUniformLocation(warm,"extra"),9);
    require(dispatch(warm)==51,"Warm binary did not accept reapplied uniforms");
    glDeleteProgram(warm);require(!glIsProgram(warm),"Transferred warm program leaked");
    GLuint owned=0;
    counts={};{
        Ge::ComputeShader shader((directory/"compute.glsl").string(),defines,"cache-owned",options);
        require(shader.isValid(),shader.getLastError());owned=shader.getProgram();
    }
    require(!glIsProgram(owned),"Cached ComputeShader destructor leaked");expected(0,0,1,"Cached destructor");

    const auto include=directory/"nested/value.glsl";const auto stamp=fs::last_write_time(include);
    write(include,"uint helper() { return 36u; }\n");fs::last_write_time(include,stamp);
    counts={};releaseCompute(directory,options,43);expected(1,1,0,"Same-size same-mtime include edit");
    counts={};releaseCompute(directory,options,43);expected(0,0,1,"Changed include reuse");
    counts={};releaseCompute(directory,options,44,"#define DEFINED_VALUE 8u\n");expected(1,1,0,"Define edit");
    write(include,"uint helper() { return UNDECLARED_CACHE_SYMBOL; }\n");
    counts={};{
        Ge::ComputeShader broken((directory/"compute.glsl").string(),defines,"cache-broken",options);
        require(!broken.isValid() && broken.getLastError().find("value.glsl")!=std::string::npos &&
                broken.getLastError().find("UNDECLARED_CACHE_SYMBOL")!=std::string::npos,
                "Changed broken include was hidden by an older binary");
    }expected(1,0,0,"Broken include");
    fs::remove(include);counts={};{
        Ge::ComputeShader missing((directory/"compute.glsl").string(),defines,"cache-missing",options);
        require(!missing.isValid() && missing.getLastError().find("value.glsl")!=std::string::npos,
                "Missing include was hidden by an older binary");
    }expected(0,0,0,"Missing include");
    write(include,"uint helper() { return 35u; }\n");
    counts={};releaseCompute(directory,options);expected(0,0,1,"Restored source reuses old binary");
    std::cout<<"PASS binary compute: cold/warm entry points, live results, ownership, uniform reset/reapply, exact include/define invalidation and failed-source diagnostics\n";
}
void checkFilesAndDriver(const fs::path& directory)
{
    prepareCompute(directory);Ge::ShaderCacheOptions options{true,(directory/"cache").string()};
    releaseCompute(directory,options);auto files=cacheFiles(options.directory);require(files.size()==1,"Corruption fixture expected one cache file");
    const auto file=files.front();const auto good=read(file);require(good.size()>88,"Cache fixture missing binary payload");
    for(unsigned mutation=0;mutation<6;++mutation)
    {
        auto damaged=good;
        if(mutation==0)damaged.resize(20); // truncated header
        if(mutation==1)damaged.back()^=0x40; // payload checksum
        if(mutation==2)damaged[24]^=1; // key mismatch
        if(mutation==3)damaged[8]^=1; // version mismatch
        if(mutation==4)std::fill(damaged.begin()+12,damaged.begin()+16,char(0xff)); // unsupported format
        if(mutation==5)std::fill(damaged.begin()+16,damaged.begin()+24,char(0xff)); // excessive/invalid length
        writeBytes(file,damaged);counts={};releaseCompute(directory,options);
        expected(1,1,0,"Damaged cache fallback");
        counts={};releaseCompute(directory,options);expected(0,0,1,"Damaged cache repaired");
    }
    counts={};rejectNextBinary=true;releaseCompute(directory,options);
    expected(1,1,1,"Driver-rejected binary fallback");
    require(counts.deleted>=2,"Rejected binary program was not released before replacement");
    counts={};releaseCompute(directory,options);expected(0,0,1,"Driver-rejected cache repaired");

    fakeRenderer=true;counts={};releaseCompute(directory,options);expected(1,1,0,"Changed renderer fingerprint");
    counts={};releaseCompute(directory,options);expected(0,0,1,"Changed renderer reuse");
    fakeRenderer=false;counts={};releaseCompute(directory,options);expected(0,0,1,"Original renderer cache retained");
    zeroFormats=true;counts={};releaseCompute(directory,options);expected(1,1,0,"Zero program binary formats");
    require(counts.save==0,"Binary export attempted with zero supported formats");zeroFormats=false;
    write(directory/"blocked","not a directory");
    Ge::ShaderCacheOptions blocked{true,(directory/"blocked"/"cache").string()};
    counts={};releaseCompute(directory,blocked);expected(1,1,0,"Unwritable cache fallback");
    counts={};releaseCompute(directory,{});expected(1,1,0,"Explicit cache disabled");
    require(counts.save==0,"Disabled cache exported a binary");
    std::cout<<"PASS binary cache fallback: truncated/corrupt/key/version/format/oversize files, driver rejection, renderer changes, unsupported driver and unwritable directory\n";
}
std::array<unsigned char,4> pixel(GLuint program)
{
    GLuint framebuffer,texture,vao;glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete graphics fixture");
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,1,1);glUseProgram(program);glDrawArrays(GL_TRIANGLES,0,3);
    std::array<unsigned char,4> rgba{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    glUseProgram(0);glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);
    glDeleteVertexArrays(1,&vao);glDeleteTextures(1,&texture);glDeleteFramebuffers(1,&framebuffer);return rgba;
}
void checkGraphicsCache(const fs::path& directory)
{
    write(directory/"vertex.glsl","#version 450 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}\n");
    write(directory/"color.glsl","vec4 color(){return vec4(COLOR,0.5,0.75,1);}\n");
    write(directory/"fragment.glsl","#version 450 core\n#include \"color.glsl\"\nout vec4 outColor;void main(){outColor=color();}\n");
    Ge::ShaderPair pair((directory/"fragment.glsl").string(),(directory/"vertex.glsl").string(),false);
    Ge::ShaderCacheOptions options{true,(directory/"cache").string()};
    for(unsigned run=0;run<2;++run)
    {
        GLuint owned=0;counts={};{
            Ge::GraphiquePipeline pipeline(nullptr,&pair,"#define COLOR 0.25",options);
            require(pipeline.isValid(),pipeline.getLastError());owned=pipeline.getProgram();
            auto rgba=pixel(owned);require(rgba[0]==64 && rgba[1]==128 && (rgba[2]==191 || rgba[2]==192) && rgba[3]==255,"Cached graphics pixel differs");
            expected(run?0:2,run?0:1,run?1:0,"Graphics cold/warm");
        }require(!glIsProgram(owned),"Cached graphics destructor leaked");
    }
    counts={};{
        Ge::GraphiquePipeline pipeline(nullptr,&pair,"#define COLOR 0.75",options);
        require(pipeline.isValid(),pipeline.getLastError());auto rgba=pixel(pipeline.getProgram());
        require(rgba[0]==191 || rgba[0]==192,"Graphics define did not change output");expected(2,1,0,"Graphics define invalidation");
    }
    // Identical source can be valid for two stages; stage tags must enter the key.
    write(directory/"stage.glsl","#version 450 core\nvoid main(){}\n");
    for(GLenum stage:{GL_VERTEX_SHADER,GL_FRAGMENT_SHADER})
    {
        std::string error;counts={};auto program=Ge::buildShaderProgram({{stage,(directory/"stage.glsl").string(),""}},error,"cache-stage",options);
        require(program!=0,error);expected(1,1,0,"Shader-stage key separation");glDeleteProgram(program);
    }
    std::cout<<"PASS binary graphics: real pixel cold/warm, defines, shader stage key separation and program ownership\n";
}
}
extern "C" int shader_binary_cache_tests(const char* directoryName,const char* modeName)
{
    try
    {
        realGetString=reinterpret_cast<GetStringFunction>(dlsym(RTLD_NEXT,"glGetString"));
        realGetInteger=reinterpret_cast<GetIntegerFunction>(dlsym(RTLD_NEXT,"glGetIntegerv"));
        require(realGetString && realGetInteger,"Cannot resolve real GL1.0 query functions");
        glewExperimental=GL_TRUE;auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR){}
        GLint formats=0;glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS,&formats);
        require(formats>0,"Program binary tests require a driver with binary formats");
        Hooks hooks;const fs::path directory(directoryName);const std::string mode(modeName);
        if(mode=="full")
        {checkComputeCache(directory/"compute");checkFilesAndDriver(directory/"fallback");checkGraphicsCache(directory/"graphics");}
        else
        {
            require(mode=="cold" || mode=="warm","Unknown cache test mode");
            if(mode=="cold")prepareCompute(directory);
            Ge::ShaderCacheOptions options{true,(directory/"cache").string()};counts={};releaseCompute(directory,options);
            expected(mode=="cold"?1:0,mode=="cold"?1:0,mode=="warm"?1:0,"Cross-process cache reuse");
            std::cout<<"PASS independent process "<<mode<<": actual compute result and expected compile/link/binary-load counts\n";
        }
        require(glGetError()==GL_NO_ERROR,"Program cache path left an OpenGL error");return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL shader binary cache: "<<error.what()<<'\n';return 1;}
}
