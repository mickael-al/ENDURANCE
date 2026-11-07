// Real EGL/GL fixture. Completion-query interception additionally proves that
// poll never asks for potentially blocking status/logs while a job is unfinished.
#include "glcore.hpp"
#include "ParallelShaderCompiler.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using Compiler = Ge::ParallelShaderCompiler;
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

PFNGLGETPROGRAMIVPROC originalProgramQuery = nullptr;
PFNGLGETSHADERIVPROC originalShaderQuery = nullptr;
bool blockProgram = false, blockShader = false;
unsigned prematureQueries = 0;
void GLAPIENTRY programQuery(GLuint object, GLenum name, GLint* value)
{
    if (name == GL_COMPLETION_STATUS_ARB && blockProgram) { *value = GL_FALSE; return; }
    if ((blockProgram || blockShader) && name != GL_COMPLETION_STATUS_ARB) ++prematureQueries;
    originalProgramQuery(object, name, value);
}
void GLAPIENTRY shaderQuery(GLuint object, GLenum name, GLint* value)
{
    if (name == GL_COMPLETION_STATUS_ARB && blockShader) { *value = GL_FALSE; return; }
    if ((blockProgram || blockShader) && name != GL_COMPLETION_STATUS_ARB) ++prematureQueries;
    originalShaderQuery(object, name, value);
}
struct QueryGuard
{
    QueryGuard()
    {
        originalProgramQuery = __glewGetProgramiv;
        originalShaderQuery = __glewGetShaderiv;
        __glewGetProgramiv = programQuery;
        __glewGetShaderiv = shaderQuery;
    }
    ~QueryGuard()
    {
        __glewGetProgramiv = originalProgramQuery;
        __glewGetShaderiv = originalShaderQuery;
        blockProgram = blockShader = false;
    }
};
void finish(Compiler& compiler, Compiler::Ticket ticket)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (compiler.state(ticket) == Compiler::State::Pending &&
        std::chrono::steady_clock::now() < deadline)
    {
        compiler.poll();
        std::this_thread::yield();
    }
    require(compiler.state(ticket) != Compiler::State::Pending, "Compiler completion timed out");
}
}

extern "C" int parallel_compiler_tests()
{
    try
    {
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        require(status == GLEW_OK || status == GLEW_ERROR_NO_GLX_DISPLAY, "GLEW initialization failed");
        while (glGetError() != GL_NO_ERROR) {}
        Compiler compiler;
        const GLboolean arb = __GLEW_ARB_parallel_shader_compile;
        const GLboolean khr = __GLEW_KHR_parallel_shader_compile;
        __GLEW_ARB_parallel_shader_compile = __GLEW_KHR_parallel_shader_compile = GL_FALSE;
        const bool unsupported = !compiler.initialize() && !compiler.supported() &&
            compiler.submitCompute("#version 430\nlayout(local_size_x=1) in;void main(){}") == 0 &&
            compiler.jobCount() == 0;
        __GLEW_ARB_parallel_shader_compile = arb;
        __GLEW_KHR_parallel_shader_compile = khr;
        require(unsupported, "Unsupported driver did not refuse compilation");
        if (!compiler.initialize(2, 2))
        {
            std::cout << "PASS unavailable extension refuses compilation without synchronous fallback\n";
            std::cout << "SKIP parallel GL execution: " << compiler.lastError() << '\n';
            return 77;
        }
        GLint threads = 0;
        glGetIntegerv(GL_MAX_SHADER_COMPILER_THREADS_ARB, &threads);
        require(threads == 2, "Compiler thread hint was not applied");
        const std::string source = "#version 430\nlayout(local_size_x=1) in;"
            "layout(std430,binding=0) buffer Values {uint result;}; void main(){result=42u;}";
        const auto good = compiler.submitCompute(source, "compute result");
        const auto bad = compiler.submitCompute("#version 430\n#line 123 7\nBROKEN_SHADER\n", "bad include");
        require(good && bad && compiler.pendingCount() == 2, "Two jobs were not accepted");
        require(compiler.submitCompute(source) == 0 && compiler.jobCount() == 2, "Capacity was not enforced");
        require(compiler.takeProgram(good) == 0, "Pending program was transferred");
        {
            QueryGuard guard;
            blockProgram = true;
            compiler.poll();
            require(prematureQueries == 0 && compiler.pendingCount() == 2,
                "poll queried statuses before program completion");
            blockProgram = false;
            blockShader = true;
            compiler.poll();
            require(prematureQueries == 0 && compiler.pendingCount() == 2,
                "poll queried statuses before shader completion");
        }
        finish(compiler, good);
        finish(compiler, bad);
        require(compiler.state(good) == Compiler::State::Ready, compiler.error(good));
        require(compiler.state(bad) == Compiler::State::Failed, "Invalid GLSL unexpectedly linked");
        require(compiler.error(bad).find("bad include") != std::string::npos &&
            compiler.error(bad).find("123") != std::string::npos,
            "Failure lost label or original #line location: " + compiler.error(bad));
        compiler.cancel(bad);
        const GLuint program = compiler.takeProgram(good);
        require(program && compiler.jobCount() == 0 && compiler.state(good) == Compiler::State::Unknown,
            "Program ownership transfer left a job");
        GLint attached = -1;
        glGetProgramiv(program, GL_ATTACHED_SHADERS, &attached);
        require(attached == 0, "Completed shader objects were not detached");
        GLuint buffer = 0, value = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(value), &value, GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
        glUseProgram(program);
        glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        // Deliberately blocking readback exists only in this test.
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(value), &value);
        require(value == 42, "Compiled program did not execute correctly");
        glUseProgram(0);
        glDeleteBuffers(1, &buffer);
        glDeleteProgram(program);

        const auto graphics = compiler.submit({
            {GL_VERTEX_SHADER, "#version 430\nvoid main(){gl_Position=vec4(0,0,0,1);}"},
            {GL_FRAGMENT_SHADER, "#version 430\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}"}
        }, "two stages");
        require(graphics != 0, "Multi-stage submission refused");
        finish(compiler, graphics);
        require(compiler.state(graphics) == Compiler::State::Ready, compiler.error(graphics));
        compiler.cancel(graphics);
        const auto pending = compiler.submitCompute(source);
        compiler.cancel(pending);
        compiler.cancel(pending); // idempotent cancellation
        require(compiler.jobCount() == 0, "Cancellation did not release job");
        require(compiler.submitCompute(source) != 0, "Capacity not reusable");
        compiler.shutdown();
        require(compiler.jobCount() == 0 && !compiler.supported(), "Shutdown retained a job");
        require(glGetError() == GL_NO_ERROR, "OpenGL error during compiler lifetime tests");
        std::cout << "PASS parallel compiler: nonblocking completion queries, capacity, diagnostics and ownership\n";
        std::cout << "PASS real GL compute output, graphics linking, cancellation, shutdown, unsupported fallback\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL parallel compiler: " << error.what() << '\n';
        return 1;
    }
}
