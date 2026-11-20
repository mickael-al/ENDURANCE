// Run the production loader, compiler and owning engine classes. Temporary
// shader files exercise real OpenGL; only one driver's oversized diagnostic
// is injected through its query entry points to verify unbounded log handling.
#include "glcore.hpp"
#include "ComputeShader.hpp"
#include "GraphiquePipeline.hpp"
#include "ShaderCompilation.hpp"
#include "ShaderBinaryCache.hpp"
#include "ShaderPair.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
void require(bool condition, const std::string& message)
{ if (!condition) throw std::runtime_error(message); }
void write(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << content;
    require(file.good(), "Cannot write temporary shader: " + path.string());
}
bool reportsLine(const std::string& diagnostic, unsigned line)
{
    const auto number=std::to_string(line);
    // Mesa/AMD use source:line; NVIDIA commonly uses source(line).
    return diagnostic.find(":"+number+"(")!=std::string::npos ||
           diagnostic.find(":"+number+":")!=std::string::npos ||
           diagnostic.find("("+number+")")!=std::string::npos;
}
std::string compute(const std::string& value)
{
    return "#version 450 core\nlayout(local_size_x=1) in;\n"
           "layout(std430,binding=0) buffer Output { uint value; };\n"
           "void main() { value=" + value + "; }\n";
}
std::uint32_t dispatch(GLuint program)
{
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    std::uint32_t result = 0;
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(result), &result, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
    glUseProgram(program);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), &result);
    glUseProgram(0);
    glDeleteBuffers(1, &buffer);
    return result;
}
const std::string vertex = "void main() { vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
const std::string fragment = "out vec4 outputColor;\nvoid main() { outputColor=vec4(COLOR_VALUE,0.5,0.75,1.0); }\n";
void draw(GLuint program)
{
    GLuint framebuffer, texture, vao;
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, "Incomplete test framebuffer");
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glViewport(0,0,1,1); glUseProgram(program); glDrawArrays(GL_TRIANGLES,0,3);
    std::array<unsigned char,4> rgba{};
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    glUseProgram(0); glBindFramebuffer(GL_FRAMEBUFFER,0); glBindVertexArray(0);
    glDeleteVertexArrays(1,&vao); glDeleteTextures(1,&texture); glDeleteFramebuffers(1,&framebuffer);
    require(rgba[0]==64 && rgba[1]==128 && (rgba[2]==191 || rgba[2]==192) && rgba[3]==255,
            "Graphics defines/includes changed the rendered value");
}
void checkLoader(const fs::path& directory)
{
    write(directory/"nested/detail/value.glsl", "uint helper() { return 35u; }\n");
    write(directory/"nested/bridge.glsl", "#include \"detail/value.glsl\"\n");
    write(directory/"nested.comp", "#version 450 core\n#include \"nested/bridge.glsl\"\n" + compute("helper()+EXTRA_VALUE").substr(18));
    Ge::ShaderSource source;
    std::string error;
    require(Ge::loadShaderSource((directory/"nested.comp").string(),"#define EXTRA_VALUE 7u",source,error),error);
    require(error.empty() && source.files.size()==3, "Nested include source map is incomplete");
    require(source.text.find("#line")!=std::string::npos && source.text.find("#include")==std::string::npos,
            "Nested includes were not expanded with source mapping");
    require(source.text.find("#version 450 core")<source.text.find("#define EXTRA_VALUE"),
            "Defines precede the shader version");
    write(directory/"commented.comp", "\xEF\xBB\xBF/* #version 100\n#include \"does-not-exist.glsl\"\n*/\n#version 450 core\n"+compute("23u").substr(18));
    require(Ge::loadShaderSource((directory/"commented.comp").string(),"#define FLAG 1",source,error),error);
    require(source.files.size()==1 && source.text.find("#version 330 core")==std::string::npos,
            "BOM or commented directives changed version/include handling");
    write(directory/"legacy.vert",vertex);
    require(Ge::loadShaderSource((directory/"legacy.vert").string(),"#define COLOR_VALUE 0.25",source,error),error);
    require(source.text.find("#version 330 core")!=std::string::npos, "Legacy fallback version changed");
    write(directory/"missing.comp","#version 450 core\n#include \"missing.glsl\"\n");
    require(!Ge::loadShaderSource((directory/"missing.comp").string(),"",source,error) && error.find("missing.glsl")!=std::string::npos,
            "Missing include was not diagnosed");
    write(directory/"cycle-a.glsl","#include \"cycle-b.glsl\"\n");
    write(directory/"cycle-b.glsl","#include \"cycle-a.glsl\"\n");
    require(!Ge::loadShaderSource((directory/"cycle-a.glsl").string(),"",source,error) && error.find("cycle-")!=std::string::npos,
            "Recursive includes were not rejected");
    std::cout << "PASS engine source loader: nested relative includes, source maps, defines, legacy version, missing and recursive includes\n";
}
void checkCompute(const fs::path& directory)
{
    GLuint released = 0;
    {
        Ge::ComputeShader shader((directory/"nested.comp").string(),"#define EXTRA_VALUE 7u");
        require(shader.isValid() && shader.getLastError().empty(),shader.getLastError());
        released=shader.getProgram();
        require(dispatch(released)==42,"Nested compute program produced the wrong value");
    }
    require(!glIsProgram(released),"ComputeShader destructor leaked its owned program");
    GLuint taken=0;
    {
        Ge::ComputeShader shader((directory/"nested.comp").string(),"#define EXTRA_VALUE 8u");
        require(shader.isValid(),shader.getLastError());
        taken=shader.takeProgram();
        require(taken!=0 && shader.getProgram()==0 && !shader.isValid(),"takeProgram did not transfer ownership");
        require(shader.takeProgram()==0,"takeProgram transferred the same program twice");
    }
    require(glIsProgram(taken) && dispatch(taken)==43,"Transferred program was deleted by ComputeShader destructor");
    glDeleteProgram(taken);
    // The compute fallback is 430, independently of legacy graphics' 330.
    write(directory/"fallback.comp",compute("19u").substr(18));
    Ge::ComputeShader fallback((directory/"fallback.comp").string());
    require(fallback.isValid() && dispatch(fallback.getProgram())==19,fallback.getLastError());
    write(directory/"nested/detail/broken.glsl","// original line one\n// original line two\nfloat broken = UNDECLARED_DIAGNOSTIC;\n");
    write(directory/"bad.comp","#version 450 core\n#include \"nested/detail/broken.glsl\"\n"+compute("1u").substr(18));
    Ge::ComputeShader bad((directory/"bad.comp").string());
    require(!bad.isValid() && bad.getProgram()==0 && bad.getLastError().find("broken.glsl")!=std::string::npos &&
            bad.getLastError().find("UNDECLARED_DIAGNOSTIC")!=std::string::npos && reportsLine(bad.getLastError(),3),
            "Compute failure lost the original nested source diagnostic");
    Ge::ComputeShader commented((directory/"commented.comp").string());
    require(commented.isValid() && dispatch(commented.getProgram())==23,commented.getLastError());
    Ge::ComputeShader missing((directory/"does-not-exist.comp").string());
    require(!missing.isValid() && missing.getProgram()==0 && missing.getLastError().find("does-not-exist.comp")!=std::string::npos,
            "Missing compute source produced an invalid owned handle");
    require(glGetError()==GL_NO_ERROR,"Compute failure/cleanup made invalid OpenGL calls");
    std::cout << "PASS ComputeShader: actual dispatch, compute fallback, diagnostics, missing files, destructor and takeProgram ownership\n";
}
void checkVersionComment(const fs::path& directory)
{
    // The version's trailing comment ends on a line that immediately resumes
    // GLSL code. Injected definitions must precede that code, outside the comment.
    const std::string prefix="#version 450 core /* version comment\n"
                             "#include \"missing-comment-only.glsl\"\n"
                             "*/ const uint fromDefine=COMMENT_VALUE";
    write(directory/"version-comment.comp",prefix+";\n"+compute("fromDefine").substr(18));
    Ge::ComputeShader good((directory/"version-comment.comp").string(),"#define COMMENT_VALUE 29u");
    require(good.isValid() && dispatch(good.getProgram())==29,good.getLastError());
    write(directory/"version-comment-bad.comp",prefix+"+UNDECLARED_AFTER_COMMENT;\n"+compute("fromDefine").substr(18));
    Ge::ComputeShader bad((directory/"version-comment-bad.comp").string(),"#define COMMENT_VALUE 29u");
    require(!bad.isValid() && bad.getProgram()==0 &&
            bad.getLastError().find("version-comment-bad.comp")!=std::string::npos &&
            bad.getLastError().find("UNDECLARED_AFTER_COMMENT")!=std::string::npos &&
            reportsLine(bad.getLastError(),3),
            "Definitions after a multiline version comment lost the original source line");
    require(bad.getLastError().find("injected shader definition | #define COMMENT_VALUE 29u")!=std::string::npos,
            "Injected definition was incorrectly attributed to an original source line");
    require(glGetError()==GL_NO_ERROR,"Version comment failure made invalid OpenGL calls");
    std::cout << "PASS version comment: defines outside multiline comment, same-line GLSL execution and original line3 diagnostic\n";
}
void checkGraphics(const fs::path& directory)
{
    write(directory/"legacy.frag",fragment);
    Ge::ShaderPair legacy((directory/"legacy.frag").string(),(directory/"legacy.vert").string(),false);
    GLuint released=0;
    {
        Ge::GraphiquePipeline pipeline(nullptr,&legacy,"#define COLOR_VALUE 0.25");
        require(pipeline.isValid() && pipeline.getLastError().empty(),pipeline.getLastError());
        require(pipeline.getShaderPair()==&legacy,"Pipeline lost its shader pair");
        released=pipeline.getProgram();draw(released);
    }
    require(!glIsProgram(released),"Graphics destructor leaked its program");
    write(directory/"shared/fragment-body.glsl",fragment);
    write(directory/"included.frag","#version 450 core\n#include \"shared/fragment-body.glsl\"\n");
    write(directory/"explicit.vert","#version 450 core\n"+vertex);
    Ge::ShaderPair included((directory/"included.frag").string(),(directory/"explicit.vert").string(),false);
    Ge::GraphiquePipeline good(nullptr,&included,"#define COLOR_VALUE 0.25");
    require(good.isValid(),good.getLastError());draw(good.getProgram());
    write(directory/"shared/bad-fragment.glsl","out vec4 outputColor;\nvoid main() { outputColor=BAD_GRAPHICS_SYMBOL; }\n");
    write(directory/"bad.frag","#version 450 core\n#include \"shared/bad-fragment.glsl\"\n");
    Ge::ShaderPair broken((directory/"bad.frag").string(),(directory/"explicit.vert").string(),false);
    {
        Ge::GraphiquePipeline bad(nullptr,&broken,"");
        require(!bad.isValid() && bad.getProgram()==0 && bad.getLastError().find("bad-fragment.glsl")!=std::string::npos && reportsLine(bad.getLastError(),2),
                "Graphics compilation failure lost its source mapping");
    }
    write(directory/"mismatch.vert","#version 450 core\nout vec3 sharedValue;\nvoid main() { sharedValue=vec3(1); gl_Position=vec4(0); }\n");
    write(directory/"mismatch.frag","#version 450 core\nin vec2 sharedValue; out vec4 outputColor;\nvoid main() { outputColor=vec4(sharedValue,0,1); }\n");
    Ge::ShaderPair mismatched((directory/"mismatch.frag").string(),(directory/"mismatch.vert").string(),false);
    {
        Ge::GraphiquePipeline bad(nullptr,&mismatched,"");
        require(!bad.isValid() && bad.getProgram()==0 && !bad.getLastError().empty(),
                "Graphics link failure produced an owned invalid program");
    }
    require(glGetError()==GL_NO_ERROR,"Graphics failure/cleanup made invalid OpenGL calls");
    std::cout << "PASS GraphiquePipeline: legacy version/defines, real pixel output, includes, compile/link failures and ownership\n";
}
std::string oversizedDiagnostic;
PFNGLGETSHADERIVPROC originalShaderQuery;
PFNGLGETSHADERINFOLOGPROC originalShaderLog;
PFNGLGETPROGRAMIVPROC originalProgramQuery;
PFNGLGETPROGRAMINFOLOGPROC originalProgramLog;
void GLAPIENTRY shaderQuery(GLuint shader, GLenum name, GLint* result)
{
    if(name==GL_INFO_LOG_LENGTH) *result=static_cast<GLint>(oversizedDiagnostic.size()+1);
    else originalShaderQuery(shader,name,result);
}
void GLAPIENTRY programQuery(GLuint program, GLenum name, GLint* result)
{
    if(name==GL_INFO_LOG_LENGTH) *result=static_cast<GLint>(oversizedDiagnostic.size()+1);
    else originalProgramQuery(program,name,result);
}
void GLAPIENTRY shaderLog(GLuint, GLsizei capacity, GLsizei* length, GLchar* text)
{
    const auto count=std::min<std::size_t>(oversizedDiagnostic.size(),capacity>0?capacity-1:0);
    if(capacity>0){std::memcpy(text,oversizedDiagnostic.data(),count);text[count]='\0';}
    if(length)*length=static_cast<GLsizei>(count);
}
struct QueryScope
{
    QueryScope(){originalShaderQuery=__glewGetShaderiv;originalShaderLog=__glewGetShaderInfoLog;
                 originalProgramQuery=__glewGetProgramiv;originalProgramLog=__glewGetProgramInfoLog;
                 __glewGetShaderiv=shaderQuery;__glewGetShaderInfoLog=shaderLog;
                 __glewGetProgramiv=programQuery;__glewGetProgramInfoLog=shaderLog;}
    ~QueryScope(){__glewGetShaderiv=originalShaderQuery;__glewGetShaderInfoLog=originalShaderLog;
                  __glewGetProgramiv=originalProgramQuery;__glewGetProgramInfoLog=originalProgramLog;}
};
void checkSourceContext(const fs::path& directory)
{
    write(directory/"macro/defines.glsl", "#define BAD_VALUE 1.5 q\n/*\n#line 999 8\n"
          "#define BAD_VALUE COMMENTED_OUT\n*/\n#define CLIMATE_VALUE BAD_VALUE\n");
    write(directory/"macro/use.glsl", "// mapped first line\n// mapped second line\n"
          "float broken = clamp(CLIMATE_VALUE, 0.0, 1.0);\n");
    write(directory/"macro.comp", "#version 450 core\n#include \"macro/defines.glsl\"\n"
          "#include \"macro/use.glsl\"\n"+compute("1u").substr(18));
    const auto stages=std::vector<Ge::ShaderFile>{{GL_COMPUTE_SHADER,(directory/"macro.comp").string(),""}};
    const auto check=[&](const std::string& error)
    {
        require(error.find(" > 3 | float broken = clamp(CLIMATE_VALUE")!=std::string::npos,
                "Source diagnostic did not show the exact line from the include snapshot");
        require(error.find("defines.glsl:1 | #define BAD_VALUE 1.5 q")!=std::string::npos &&
                error.find("defines.glsl:6 | #define CLIMATE_VALUE BAD_VALUE")!=std::string::npos,
                "Source diagnostic lost recursively referenced macro definitions");
        require(error.find("COMMENTED_OUT")==std::string::npos,
                "Commented directives corrupted the diagnostic source/macro map");
    };
    std::string error;
    require(Ge::buildShaderProgram(stages,error)==0,"Malformed macro unexpectedly compiled");
    check(error);
    // Preserve a genuine failed compilation, substituting only the driver log
    // format. This exercises NVIDIA and colon-separated locations on any GPU.
    for(const auto* diagnostic : {"2(3) : error C0000: unexpected identifier q\n",
                                  "ERROR: 2:3: unexpected identifier q\n"})
    {
        oversizedDiagnostic=diagnostic;
        QueryScope scope;
        require(Ge::buildShaderProgram(stages,error)==0,"Malformed macro unexpectedly compiled");
        check(error);
        require(error.find(diagnostic)!=std::string::npos,"Original driver diagnostic was changed");
    }
    std::cout << "PASS source context: original include lines, recursive macro values, ignored comment directives, NVIDIA/Mesa/AMD locations\n";
}
void checkTerrainEntryPoints(const fs::path& root)
{
    // Climate values and source layout are intentionally user-editable. Derive
    // this negative control from the current file instead of freezing its tune.
    const std::string macro="ENDURANCE_CLIMATE_CONTRAST";
    std::ifstream climate(root/"Asset/Shader/Noise/climate_2d.glsl");
    require(climate.good(),"Cannot read climate regression source");
    std::string line, definition;
    unsigned lineNumber=0, definitionLine=0, useLine=0;
    while(std::getline(climate,line))
    {
        ++lineNumber;
        std::istringstream tokens(line);
        std::string first, name;
        tokens>>first>>name;
        if(first=="#define" && name==macro)
        {definition=line;definitionLine=lineNumber;}
        else if(!first.empty() && first[0]!='#' && first.compare(0,2,"//")!=0 &&
                line.find(macro)!=std::string::npos && !useLine)
            useLine=lineNumber;
    }
    require(definitionLine && useLine,"Climate contrast definition/use is missing");
    const auto comment=(std::min)(definition.find("//"),definition.find("/*"));
    const auto end=definition.find_last_not_of(" \t\r",comment==std::string::npos ? comment : comment-1)+1;
    std::string malformedDefinition=definition;
    malformedDefinition.insert(end," q");
    for(const auto* name : {"Asset/Shader/ProceduralTerrain/field_batch_classify.C.glsl",
                            "ENDURANCE/Ressources/Shader/Atmosphere/palette.C.glsl"})
    {
        const std::vector<Ge::ShaderFile> stages{{GL_COMPUTE_SHADER,(root/name).string(),"#define EMIT_PASS 0\n"}};
        std::string error;
        auto program=Ge::buildShaderProgram(stages,error);
        require(program!=0,error);
        glDeleteProgram(program);
        Ge::ShaderSource source;
        require(Ge::loadShaderSource(stages[0].filename,stages[0].defines,source,error,"430 core"),error);
        const auto location=source.text.find(definition+"\n");
        require(location!=std::string::npos,"Climate contrast regression fixture needs updating");
        // Mutate only the prepared in-memory snapshot, never an application file.
        source.text.replace(location,definition.size(),malformedDefinition);
        const std::vector<Ge::ShaderSource> sources{source};
        Ge::detail::ShaderBinaryCache cache(stages,sources,{},"");
        program=Ge::detail::buildPreparedShaderProgram(stages,sources,error,"",cache);
        require(program==0,"Invalid climate contrast unexpectedly compiled");
        require(error.find("climate_2d.glsl:"+std::to_string(useLine))!=std::string::npos &&
                error.find("climate_2d.glsl:"+std::to_string(definitionLine)+" | "+malformedDefinition)!=std::string::npos,
                "Climate failure lost its original line and malformed macro definition: "+error);
        std::cout << "PASS production entrypoint and malformed climate macro diagnostic: " << name << '\n';
    }
    require(glGetError()==GL_NO_ERROR,"Production entrypoint checks made invalid OpenGL calls");
}
void checkLongDiagnostic(const fs::path& directory)
{
    oversizedDiagnostic="DRIVER_LOG_BEGIN\n"+std::string(131073,'x')+"\nDRIVER_LOG_END\n";
    QueryScope scope;
    std::string error;
    const auto program=Ge::buildShaderProgram({{GL_COMPUTE_SHADER,(directory/"bad.comp").string(),""}},error);
    require(program==0 && error.find(oversizedDiagnostic)!=std::string::npos,
            "Long driver diagnostic was truncated or discarded");
    const auto mismatched=Ge::buildShaderProgram({
        {GL_VERTEX_SHADER,(directory/"mismatch.vert").string(),""},
        {GL_FRAGMENT_SHADER,(directory/"mismatch.frag").string(),""}},error);
    require(mismatched==0 && error.find(oversizedDiagnostic)!=std::string::npos,
            "Long linker diagnostic was truncated or discarded");
    require(glGetError()==GL_NO_ERROR,"Long diagnostic cleanup made invalid OpenGL calls");
    std::cout << "PASS engine diagnostics: 128 KiB compiler/linker logs retained completely without Debug/Console dependencies\n";
}
}
extern "C" int shader_compilation_tests(const char* temporaryDirectory, const char* repositoryRoot)
{
    try
    {
        glewExperimental=GL_TRUE;
        const auto status=glewInit();
        require(status==GLEW_OK || status==GLEW_ERROR_NO_GLX_DISPLAY,"GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR){}
        const fs::path directory(temporaryDirectory);
        checkLoader(directory);checkCompute(directory);checkVersionComment(directory);checkGraphics(directory);
        checkSourceContext(directory);checkTerrainEntryPoints(fs::path(repositoryRoot));checkLongDiagnostic(directory);
        return 0;
    }
    catch(const std::exception& error)
    {std::cerr << "FAIL engine shader compilation: " << error.what() << '\n';return 1;}
}
