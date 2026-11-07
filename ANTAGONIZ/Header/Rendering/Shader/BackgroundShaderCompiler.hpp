#ifndef ENDURANCE_BACKGROUND_SHADER_COMPILER_HPP
#define ENDURANCE_BACKGROUND_SHADER_COMPILER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Ge
{
    // One worker owns a context shared with the rendering context. Context
    // creation/destruction belongs to the caller (GLFW, EGL, etc.), not here.
    // initialize/shutdown are lifecycle operations and may wait; submit, poll,
    // state and takeProgram never compile, query, or wait on OpenGL on the caller.
    class BackgroundShaderCompiler
    {
    public:
        using Ticket = std::uint64_t;
        enum class State { Unknown, Pending, Ready, Failed };
        struct Source { unsigned int stage = 0; std::string text; };
        struct ContextHooks
        {
            // Executed on the worker. makeCurrent must leave the caller's
            // rendering context untouched. Both hooks must remain valid until
            // shutdown joins the worker, before the shared context is destroyed.
            std::function<bool()> makeCurrent;
            std::function<void()> releaseCurrent;
        };
        using PrepareProgram = std::function<void(unsigned int)>;

        BackgroundShaderCompiler();
        ~BackgroundShaderCompiler();
        BackgroundShaderCompiler(const BackgroundShaderCompiler&) = delete;
        BackgroundShaderCompiler& operator=(const BackgroundShaderCompiler&) = delete;

        bool initialize(ContextHooks hooks, std::size_t maxJobs = 2);
        void shutdown() noexcept;
        bool supported() const noexcept;

        // Preparation runs on the worker after linking and before publication.
        // It can discover uniforms, initialize them and warm up the program.
        // An exception makes the job Failed and deletes its GL objects there.
        // Preparation must not call initialize/shutdown on this compiler.
        Ticket submit(std::vector<Source> sources, std::string label = {}, PrepareProgram prepare = {});
        Ticket submitCompute(std::string source, std::string label = {}, PrepareProgram prepare = {});
        void poll() noexcept; // CPU-only compatibility entry point; worker makes progress itself.
        State state(Ticket ticket) const noexcept;
        std::string error(Ticket ticket) const;
        std::string lastError() const;
        std::size_t pendingCount() const noexcept;
        std::size_t jobCount() const noexcept;

        // Ready means linking/preparation and worker GL completion have finished.
        // Rebind with glUseProgram in the consumer's shared context before use.
        // The caller assumes ownership and must eventually glDeleteProgram it.
        unsigned int takeProgram(Ticket ticket) noexcept;
        // Cancellation is nonblocking. Cancelled jobs occupy their bounded slot
        // until the worker has discarded them; an in-progress driver call cannot
        // be interrupted. Ready programs are also deleted on the worker.
        void cancel(Ticket ticket) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

#endif
