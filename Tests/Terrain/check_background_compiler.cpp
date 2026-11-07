#include "glcore.hpp"
#include "BackgroundShaderCompiler.hpp"
#include "SharedEglContext.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using Compiler = Ge::BackgroundShaderCompiler;
using Clock = std::chrono::steady_clock;
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate> void waitFor(Predicate predicate, const char* message)
{
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!predicate() && Clock::now() < deadline) std::this_thread::yield();
    require(predicate(), message);
}
void finish(Compiler& compiler, Compiler::Ticket ticket)
{
    waitFor([&] { compiler.poll(); return compiler.state(ticket) != Compiler::State::Pending; },
        "Background shader completion timed out");
}
std::thread::id caller;
std::atomic<unsigned> callerGLCalls{0}, workerGLCalls{0};
PFNGLCOMPILESHADERPROC originalCompile = nullptr;
PFNGLLINKPROGRAMPROC originalLink = nullptr;
PFNGLGETPROGRAMIVPROC originalProgramQuery = nullptr;
PFNGLGETSHADERIVPROC originalShaderQuery = nullptr;
void record() { (std::this_thread::get_id() == caller ? callerGLCalls : workerGLCalls).fetch_add(1); }
void GLAPIENTRY compileShader(GLuint shader) { record(); originalCompile(shader); }
void GLAPIENTRY linkProgram(GLuint program) { record(); originalLink(program); }
void GLAPIENTRY programQuery(GLuint program, GLenum name, GLint* value)
{ record(); originalProgramQuery(program, name, value); }
void GLAPIENTRY shaderQuery(GLuint shader, GLenum name, GLint* value)
{ record(); originalShaderQuery(shader, name, value); }
struct Intercept
{
    Intercept()
    {
        caller = std::this_thread::get_id();
        originalCompile = __glewCompileShader; __glewCompileShader = compileShader;
        originalLink = __glewLinkProgram; __glewLinkProgram = linkProgram;
        originalProgramQuery = __glewGetProgramiv; __glewGetProgramiv = programQuery;
        originalShaderQuery = __glewGetShaderiv; __glewGetShaderiv = shaderQuery;
    }
    ~Intercept()
    {
        __glewCompileShader = originalCompile; __glewLinkProgram = originalLink;
        __glewGetProgramiv = originalProgramQuery; __glewGetShaderiv = originalShaderQuery;
    }
};
struct Gate
{
    std::mutex mutex;
    std::condition_variable wake;
    bool entered = false, release = false;
    void block()
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        wake.notify_all();
        // Automatic release keeps a broken caller-blocking implementation from
        // hanging the fixture; the latency assertion then reports the failure.
        wake.wait_for(lock, std::chrono::milliseconds(500), [&] { return release; });
    }
    void waitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(wake.wait_for(lock, std::chrono::seconds(10), [&] { return entered; }),
            "Preparation callback did not run");
    }
    void open()
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        wake.notify_all();
    }
};
}

extern "C" int background_compiler_tests()
{
    try
    {
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        require(status == GLEW_OK || status == GLEW_ERROR_NO_GLX_DISPLAY, "GLEW initialization failed");
        while (glGetError() != GL_NO_ERROR) {}
        SharedEglContext context;
        Intercept interception;
        Compiler compiler;
        require(!compiler.initialize({}, 2), "Missing context hooks were accepted");
        require(!compiler.initialize({[] { return false; }, [] {}}, 2), "Context activation failure ignored");
        require(!compiler.initialize({[]() -> bool { throw std::runtime_error("context failure"); }, [] {}}, 2),
            "Context activation exception ignored");
        require(compiler.lastError().find("context failure") != std::string::npos,
            "Context activation diagnostics lost");
        require(compiler.initialize(context.hooks(), 2), compiler.lastError());
        require(compiler.supported(), "Compiler did not become available");
        const std::string source = "#version 430\nlayout(local_size_x=1) in;"
            "uniform uint uValue;layout(std430,binding=0) buffer Values {uint result;};"
            "void main(){result=uValue;}";
        Gate gate;
        std::atomic<bool> preparedOnWorker{false};
        const auto first = compiler.submitCompute(source, "shared compute", [&](GLuint program) {
            preparedOnWorker = std::this_thread::get_id() != caller;
            glUseProgram(program);
            glUniform1ui(glGetUniformLocation(program, "uValue"), 73u);
            gate.block();
        });
        require(first != 0, compiler.lastError());
        gate.waitEntered();
        const auto start = Clock::now();
        const auto cancelled = compiler.submitCompute(source, "queued cancellation");
        require(cancelled != 0, compiler.lastError());
        require(compiler.submitCompute(source) == 0, "Capacity was not enforced");
        compiler.cancel(cancelled);
        require(compiler.state(cancelled) == Compiler::State::Unknown, "Cancelled ticket still visible");
        for (unsigned i = 0; i < 10000; ++i)
        {
            compiler.poll();
            require(compiler.state(first) == Compiler::State::Pending, "Program published before preparation ended");
            require(compiler.takeProgram(first) == 0, "Unfinished program transferred");
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        gate.open();
        require(elapsed < 200.0, "Caller blocked behind worker compilation/preparation");
        finish(compiler, first);
        require(preparedOnWorker && compiler.state(first) == Compiler::State::Ready,
            "Preparation did not execute successfully on worker");
        const GLuint program = compiler.takeProgram(first);
        waitFor([&] { return compiler.jobCount() == 0; }, "Cancelled queued job was not reaped");
        require(program && glIsProgram(program), "Shared program is unavailable on caller context");
        GLuint buffer = 0, value = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(value), &value, GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
        glUseProgram(program);
        glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(value), &value);
        require(value == 73u, "Shared program/uniform initialization not visible to consumer");
        glUseProgram(0);
        glDeleteBuffers(1, &buffer);
        glDeleteProgram(program);

        const auto bad = compiler.submitCompute("#version 430\n#line 123 7\nBROKEN_SHADER\n", "original include");
        finish(compiler, bad);
        require(compiler.state(bad) == Compiler::State::Failed &&
            compiler.error(bad).find("original include") != std::string::npos &&
            compiler.error(bad).find("123") != std::string::npos, "GLSL diagnostics lost original location");
        compiler.cancel(bad);
        waitFor([&] { return compiler.jobCount() == 0; }, "Failed job cancellation did not free capacity");
        std::atomic<GLuint> failedProgram{0};
        const auto failedPreparation = compiler.submitCompute(source, "preparation", [&](GLuint candidate) {
            failedProgram = candidate;
            glUseProgram(candidate);
            throw std::runtime_error("preparation failure");
        });
        finish(compiler, failedPreparation);
        require(compiler.state(failedPreparation) == Compiler::State::Failed &&
            compiler.error(failedPreparation).find("preparation failure") != std::string::npos,
            "Preparation exception did not fail the job");
        require(!glIsProgram(failedProgram), "Failed preparation retained a current/deleted program");
        compiler.cancel(failedPreparation);
        waitFor([&] { return compiler.jobCount() == 0; }, "Preparation failure slot not reclaimed");

        const auto graphics = compiler.submit({
            {GL_VERTEX_SHADER, "#version 430\nvoid main(){gl_Position=vec4(0,0,0,1);}"},
            {GL_FRAGMENT_SHADER, "#version 430\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}"}
        }, "shared graphics");
        finish(compiler, graphics);
        require(compiler.state(graphics) == Compiler::State::Ready, compiler.error(graphics));
        compiler.cancel(graphics);
        waitFor([&] { return compiler.jobCount() == 0; }, "Ready program cancellation did not free capacity");

        Gate runningGate;
        std::atomic<GLuint> cancelledProgram{0};
        const auto running = compiler.submitCompute(source, "running cancellation", [&](GLuint candidate) {
            cancelledProgram = candidate;
            runningGate.block();
        });
        runningGate.waitEntered();
        compiler.cancel(running);
        require(compiler.state(running) == Compiler::State::Unknown, "Running cancellation not recorded");
        runningGate.open();
        waitFor([&] { return compiler.jobCount() == 0; }, "Running cancellation did not complete");
        require(!glIsProgram(cancelledProgram), "Cancelled program leaked");
        require(compiler.submitCompute(source) != 0, "Compiler capacity not reusable");
        compiler.shutdown();
        require(!compiler.supported() && compiler.jobCount() == 0, "Shutdown did not join/clear worker");
        require(callerGLCalls == 0 && workerGLCalls > 0, "Compilation/link/status GL call ran on caller thread");
        require(glGetError() == GL_NO_ERROR, "OpenGL error in consumer context");
        std::cout << "PASS background compiler: all compile/link/status operations run on dedicated worker\n";
        std::cout << "PASS caller submit/poll during blocked preparation: " << elapsed << " ms for 10000 polls\n";
        std::cout << "PASS shared EGL program and initialized uniforms, diagnostics, bounded cancellation, exceptions, shutdown\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL background compiler: " << error.what() << '\n';
        return 1;
    }
}
