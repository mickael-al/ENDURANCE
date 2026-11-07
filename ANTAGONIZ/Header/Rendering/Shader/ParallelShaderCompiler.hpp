#ifndef ENDURANCE_PARALLEL_SHADER_COMPILER_HPP
#define ENDURANCE_PARALLEL_SHADER_COMPILER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ge
{
    // Driver-assisted parallel compilation, additive to ComputeShader/ShaderPair.
    // All methods and destruction require the owning OpenGL context to be current.
    // This class creates no context and calls no OpenGL entry point from a worker.
    class ParallelShaderCompiler
    {
    public:
        using Ticket = std::uint64_t;
        enum class State { Unknown, Pending, Ready, Failed };
        struct Source
        {
            unsigned int stage = 0; // GL_COMPUTE_SHADER, GL_VERTEX_SHADER, ...
            std::string text;       // Expanded GLSL; existing #line IDs are preserved.
        };

        ParallelShaderCompiler();
        ~ParallelShaderCompiler();
        ParallelShaderCompiler(const ParallelShaderCompiler&) = delete;
        ParallelShaderCompiler& operator=(const ParallelShaderCompiler&) = delete;

        // False means no usable ARB/KHR_parallel_shader_compile support. In that
        // case submit() refuses work: callers can use an already linked fallback
        // without silently performing synchronous compilation during gameplay.
        bool initialize(std::size_t maxJobs = 4, unsigned int compilerThreads = 2);
        void shutdown() noexcept;
        bool supported() const noexcept;

        // A zero ticket means refused (unsupported, full, or invalid request).
        // Capacity includes completed jobs until takeProgram() or cancel().
        Ticket submit(const std::vector<Source>& sources, const std::string& label = {});
        Ticket submitCompute(const std::string& source, const std::string& label = {});

        // Only completion-status queries are issued before compilation/linking
        // finish. No glFinish, wait, or premature LINK_STATUS query is performed.
        // Driver parallelism is an extension hint, not a latency guarantee.
        void poll();
        State state(Ticket ticket) const noexcept;
        const std::string& error(Ticket ticket) const noexcept;
        const std::string& lastError() const noexcept;
        std::size_t pendingCount() const noexcept;
        std::size_t jobCount() const noexcept;

        // Transfers a Ready program to the caller, who must glDeleteProgram it.
        // Other states return zero and leave the job unchanged.
        unsigned int takeProgram(Ticket ticket) noexcept;
        void cancel(Ticket ticket) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

#endif
