// Exercise the production startup batch against a real OpenGL driver. Entry
// point counters observe compilation/cache reuse and ownership without replacing
// compilation, binary loading, linking, or shader execution.
#include "glcore.hpp"
#include "ShaderProgramBatch.hpp"

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using GetStringFunction=const GLubyte* (GLAPIENTRY*)(GLenum);
    using GetIntegerFunction=void (GLAPIENTRY*)(GLenum,GLint*);
    GetStringFunction getStringOriginal=nullptr;
    GetIntegerFunction getIntegerOriginal=nullptr;
    unsigned identityQueries=0, formatCountQueries=0, formatListQueries=0;
}
// Observe the production cache's GL 1.0 queries without replacing their values.
extern "C" const GLubyte* GLAPIENTRY glGetString(GLenum name)
{
    if (name==GL_VENDOR || name==GL_RENDERER || name==GL_VERSION || name==GL_SHADING_LANGUAGE_VERSION)
        ++identityQueries;
    return getStringOriginal(name);
}
extern "C" void GLAPIENTRY glGetIntegerv(GLenum name,GLint* value)
{
    if (name==GL_NUM_PROGRAM_BINARY_FORMATS) ++formatCountQueries;
    if (name==GL_PROGRAM_BINARY_FORMATS) ++formatListQueries;
    getIntegerOriginal(name,value);
}

namespace
{
    namespace fs = std::filesystem;
    void require(bool condition, const std::string& message)
    { if (!condition) throw std::runtime_error(message); }
    void write(const fs::path& path, const std::string& content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << content;
        require(output.good(), "Cannot write test shader: " + path.string());
    }

    PFNGLCOMPILESHADERPROC compileOriginal = nullptr;
    PFNGLLINKPROGRAMPROC linkOriginal = nullptr;
    PFNGLPROGRAMBINARYPROC binaryOriginal = nullptr;
    PFNGLCREATEPROGRAMPROC createProgramOriginal = nullptr;
    PFNGLCREATESHADERPROC createShaderOriginal = nullptr;
    PFNGLDELETESHADERPROC deleteShaderOriginal = nullptr;
    PFNGLPROGRAMPARAMETERIPROC parameterOriginal = nullptr;
    unsigned compileCalls = 0, linkCalls = 0, binaryCalls = 0;
    unsigned activeShaders = 0, peakShaders = 0, missingHints = 0;
    std::vector<GLuint> createdPrograms, createdShaders;
    std::set<GLuint> binaryHints;

    void GLAPIENTRY compile(GLuint shader) { ++compileCalls; compileOriginal(shader); }
    void GLAPIENTRY link(GLuint program)
    {
        ++linkCalls;
        if (!binaryHints.count(program)) ++missingHints;
        linkOriginal(program);
    }
    void GLAPIENTRY binary(GLuint program, GLenum format, const void* bytes, GLsizei length)
    { ++binaryCalls; binaryOriginal(program, format, bytes, length); }
    GLuint GLAPIENTRY createProgram()
    {
        const auto program = createProgramOriginal();
        if (program) { createdPrograms.push_back(program); binaryHints.erase(program); }
        return program;
    }
    GLuint GLAPIENTRY createShader(GLenum stage)
    {
        const auto shader = createShaderOriginal(stage);
        if (shader)
        {
            createdShaders.push_back(shader);
            peakShaders = (std::max)(peakShaders, ++activeShaders);
        }
        return shader;
    }
    void GLAPIENTRY deleteShader(GLuint shader)
    { if (shader) --activeShaders; deleteShaderOriginal(shader); }
    void GLAPIENTRY parameter(GLuint program, GLenum name, GLint value)
    {
        if (name == GL_PROGRAM_BINARY_RETRIEVABLE_HINT && value == GL_TRUE)
            binaryHints.insert(program);
        parameterOriginal(program, name, value);
    }
    struct Observe
    {
        Observe()
        {
            compileOriginal = __glewCompileShader; __glewCompileShader = compile;
            linkOriginal = __glewLinkProgram; __glewLinkProgram = link;
            binaryOriginal = __glewProgramBinary; __glewProgramBinary = binary;
            createProgramOriginal = __glewCreateProgram; __glewCreateProgram = createProgram;
            createShaderOriginal = __glewCreateShader; __glewCreateShader = createShader;
            deleteShaderOriginal = __glewDeleteShader; __glewDeleteShader = deleteShader;
            parameterOriginal = __glewProgramParameteri; __glewProgramParameteri = parameter;
        }
        ~Observe()
        {
            __glewCompileShader = compileOriginal; __glewLinkProgram = linkOriginal;
            __glewProgramBinary = binaryOriginal; __glewCreateProgram = createProgramOriginal;
            __glewCreateShader = createShaderOriginal; __glewDeleteShader = deleteShaderOriginal;
            __glewProgramParameteri = parameterOriginal;
        }
        void reset()
        {
            require(activeShaders == 0, "Previous batch retained shader objects");
            compileCalls = linkCalls = binaryCalls = peakShaders = missingHints = 0;
            identityQueries=formatCountQueries=formatListQueries=0;
            createdPrograms.clear(); createdShaders.clear(); binaryHints.clear();
        }
        void noLeaks()
        {
            require(activeShaders == 0, "Batch leaked active shader objects");
            for (const auto shader : createdShaders)
                require(!glIsShader(shader), "Batch retained a shader object after completion/failure");
            for (const auto program : createdPrograms)
                require(!glIsProgram(program), "Batch retained a program after completion/failure");
            require(glGetError() == GL_NO_ERROR, "Batch produced an OpenGL error");
        }
    };
    struct ForceSynchronous
    {
        const GLboolean arb = __GLEW_ARB_parallel_shader_compile;
        const GLboolean khr = __GLEW_KHR_parallel_shader_compile;
        ForceSynchronous() { __GLEW_ARB_parallel_shader_compile = __GLEW_KHR_parallel_shader_compile = GL_FALSE; }
        ~ForceSynchronous() { __GLEW_ARB_parallel_shader_compile = arb; __GLEW_KHR_parallel_shader_compile = khr; }
    };
    struct Programs
    {
        std::vector<unsigned int> ids;
        ~Programs() { for (const auto id : ids) glDeleteProgram(id); }
    };
    std::uint32_t execute(GLuint program)
    {
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        std::uint32_t value = 0;
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(value), &value, GL_DYNAMIC_COPY);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
        glUseProgram(program);
        glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(value), &value);
        glUseProgram(0);
        glDeleteBuffers(1, &buffer);
        return value;
    }
    Ge::ShaderProgramRequest request(const fs::path& file, const std::string& defines = {})
    { return {{{GL_COMPUTE_SHADER, file.string(), defines}}, file.filename().string()}; }

    void checkSnapshot(const fs::path& directory)
    {
        write(directory/"shared.glsl", "#ifndef SHARED\n#define SHARED\nuint sharedValue=9u;\n#endif\n");
        write(directory/"first.comp", "#version 450 core\n#include \"shared.glsl\"\n#include \"shared.glsl\"\n");
        write(directory/"second.comp", "#version 450 core\n#include \"shared.glsl\"\n");
        Ge::ShaderSourceSnapshot snapshot;
        Ge::ShaderSource first, second, independent;
        std::string error;
        require(Ge::loadShaderSource((directory/"first.comp").string(), "#define FIRST 1", first, error, "430 core", &snapshot),error);
        require(Ge::loadShaderSource((directory/"second.comp").string(), "", second, error, "430 core", &snapshot),error);
        require(snapshot.fileCount()==3,"Shared includes were not retained once per build snapshot");
        require(Ge::loadShaderSource((directory/"first.comp").string(), "#define FIRST 1", independent, error),error);
        require(first.text==independent.text && first.files==independent.files,
                "Snapshot expansion changed source bytes, definitions or source IDs");
        const auto stamp=fs::last_write_time(directory/"shared.glsl");
        write(directory/"shared.glsl", "#ifndef SHARED\n#define SHARED\nuint sharedValue=7u;\n#endif\n");
        fs::last_write_time(directory/"shared.glsl",stamp);
        require(Ge::loadShaderSource((directory/"second.comp").string(), "", second, error, "430 core", &snapshot),error);
        require(second.text.find("sharedValue=9u")!=std::string::npos,
                "One build observed different versions of its shared include");
        require(Ge::loadShaderSource((directory/"second.comp").string(), "", independent, error),error);
        require(independent.text.find("sharedValue=7u")!=std::string::npos,
                "Fresh build missed an include edit with unchanged length/timestamp");
        fs::remove(directory/"shared.glsl");
        require(!Ge::loadShaderSource((directory/"second.comp").string(), "", independent, error),
                "Fresh build hid a missing include with a previous snapshot");
        std::cout<<"PASS build-local source snapshot: shared includes, exact expansion, coherent batch, fresh edits and deletions\n";
    }

    void check(const fs::path& directory)
    {
        Ge::ShaderCacheOptions cache;
        cache.enabled = true;
        cache.directory = (directory / "cache").string();
        std::string error;
        std::vector<Ge::ShaderProgramRequest> requests;
        for (unsigned i = 0; i < 11; ++i)
        {
            const auto stem = "biome-" + std::to_string(i);
            write(directory / (stem + ".glsl"), "uint sampleBiome() { return " + std::to_string(i + 10) + "u; }\n");
            write(directory / (stem + ".comp"), "#version 450 core\n#include \"" + stem + ".glsl\"\n"
                "layout(local_size_x=1) in;\nlayout(std430,binding=0) buffer Result {uint result;};\n"
                "void main() { result=sampleBiome()+EXTRA; }\n");
            requests.push_back(request(directory / (stem + ".comp"), "#define EXTRA 1u\n"));
        }
        const bool parallel = GLEW_ARB_parallel_shader_compile || GLEW_KHR_parallel_shader_compile;
        GLint64 previousThreads = 0, restoredThreads = 0;
        if (parallel) glGetInteger64v(GL_MAX_SHADER_COMPILER_THREADS_ARB, &previousThreads);
        Observe observe;
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms(requests, error, cache, 2)};
            require(programs.ids.size() == requests.size(), error);
            require(compileCalls == requests.size() && linkCalls == requests.size() && binaryCalls == 0,
                "Cold batch did not compile exactly one program per request");
            require(identityQueries==4 && formatCountQueries==1 && formatListQueries==1,
                "Cold batch queried driver identity or binary formats more than once");
            require(missingHints == 0, "Binary retrievable hint was not set before linking");
            require(peakShaders <= 8 && peakShaders > 0, "Parallel compilation queue exceeded its bound");
            for (std::size_t i = 0; i < programs.ids.size(); ++i)
                require(execute(programs.ids[i]) == i + 11, "Batch changed the program output/order");
        }
        observe.noLeaks();
        if (parallel)
        {
            glGetInteger64v(GL_MAX_SHADER_COMPILER_THREADS_ARB, &restoredThreads);
            require(previousThreads == restoredThreads, "Batch did not restore the previous thread hint");
        }
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms(requests, error, cache)};
            require(programs.ids.size() == requests.size(), error);
            require(compileCalls == 0 && linkCalls == 0 && binaryCalls == requests.size(),
                "Warm batch recompiled a cached program");
            require(identityQueries==4 && formatCountQueries==1 && formatListQueries==1,
                "Warm batch queried driver identity or binary formats more than once");
            for (std::size_t i = 0; i < programs.ids.size(); ++i)
                require(execute(programs.ids[i]) == i + 11, "Cached batch output/order changed");
        }
        observe.noLeaks();
        write(directory / "biome-3.glsl", "uint sampleBiome() { return 100u; }\n");
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms(requests, error, cache)};
            require(programs.ids.size() == requests.size(), error);
            require(compileCalls == 1 && linkCalls == 1 && binaryCalls == requests.size() - 1,
                "Editing one biome invalidated unrelated program dependencies");
            require(execute(programs.ids[3]) == 101, "Changed biome executable was stale");
        }
        observe.noLeaks();
        requests[6].stages[0].defines = "#define EXTRA 9u\n";
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms(requests, error, cache)};
            require(programs.ids.size() == requests.size(), error);
            require(compileCalls == 1 && binaryCalls == requests.size() - 1,
                "Changing one variant's defines did not invalidate exactly that program");
            require(execute(programs.ids[6]) == 25, "Changed defines produced stale binary output");
        }
        observe.noLeaks();
        std::cout << "PASS startup shader batch: bounded compilation, output order, cache hits, isolated includes/defines, thread hint restoration\n";

        // A cached executable must not hide a now-missing selected dependency.
        fs::remove(directory / "biome-8.glsl");
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms(requests, error, cache)};
            require(programs.ids.empty() && error.find("biome-8.glsl") != std::string::npos,
                "Missing include was hidden by cached output");
        }
        observe.noLeaks();
        write(directory / "biome-8.glsl", "uint sampleBiome() { return 18u; }\n");
        write(directory / "broken.glsl", "// First line\n// Second line\nINVALID_GLSL_HERE\n");
        write(directory / "bad-batch.comp", "#version 450 core\n#include \"broken.glsl\"\n"
            "layout(local_size_x=1) in;\nvoid main() {}\n");
        auto failing = requests;
        failing[4] = request(directory / "bad-batch.comp");
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms(failing, error, {})};
            require(programs.ids.empty(), "Failed batch published a partial program set");
            require(error.find("bad-batch.comp") != std::string::npos &&
                    error.find("broken.glsl") != std::string::npos &&
                    error.find("source ID : file") != std::string::npos,
                "Failed batch omitted the label or numbered include source table: " + error);
        }
        observe.noLeaks();
        observe.reset();
        {
            Programs programs{Ge::buildShaderPrograms({}, error, cache)};
            require(programs.ids.empty() && !error.empty(), "Empty batch was not rejected");
        }
        observe.noLeaks();
        std::cout << "PASS shader batch failure: missing dependency, GLSL include diagnostics, no leaked programs/stages or partial publication\n";

        // Force the documented startup fallback without modifying the driver.
        cache.directory = (directory / "fallback-cache").string();
        {
            ForceSynchronous synchronous;
            const std::vector<Ge::ShaderProgramRequest> one{requests[0]};
            observe.reset();
            {
                Programs programs{Ge::buildShaderPrograms(one, error, cache)};
                require(programs.ids.size() == 1 && execute(programs.ids[0]) == 11, error);
                require(compileCalls == 1 && linkCalls == 1 && !missingHints,
                    "Synchronous startup fallback did not compile/cache correctly");
            }
            observe.noLeaks();
            observe.reset();
            {
                Programs programs{Ge::buildShaderPrograms(one, error, cache)};
                require(programs.ids.size() == 1 && execute(programs.ids[0]) == 11, error);
                require(compileCalls == 0 && linkCalls == 0 && binaryCalls == 1,
                    "Synchronous fallback did not restore its cached binary");
            }
            observe.noLeaks();
        }
        std::cout << "PASS startup fallback: unavailable parallel extension retains binary-cache reuse\n";
    }
}

extern "C" int shader_batch_tests(const char* directory)
{
    try
    {
        getStringOriginal=reinterpret_cast<GetStringFunction>(dlsym(RTLD_NEXT,"glGetString"));
        getIntegerOriginal=reinterpret_cast<GetIntegerFunction>(dlsym(RTLD_NEXT,"glGetIntegerv"));
        require(getStringOriginal && getIntegerOriginal,"Cannot resolve original GL queries");
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        require(status == GLEW_OK || status == GLEW_ERROR_NO_GLX_DISPLAY, "GLEW initialization failed");
        while (glGetError() != GL_NO_ERROR) {}
        GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        require(formats > 0, "Shader batch cache fixture requires a program binary format");
        checkSnapshot(fs::path(directory)/"snapshot");
        check(fs::path(directory));
        return 0;
    }
    catch (const std::exception& failure)
    { std::cerr << "FAIL shader batch: " << failure.what() << '\n'; return 1; }
}
