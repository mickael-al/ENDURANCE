#include "glcore.hpp"
#include "ParallelShaderCompiler.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace Ge
{
    namespace
    {
        std::string objectLog(GLuint object, bool program)
        {
            GLint length = 0;
            if (program) glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
            else glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
            if (length <= 1) return {};
            std::string log(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            if (program) glGetProgramInfoLog(object, length, &written, &log[0]);
            else glGetShaderInfoLog(object, length, &written, &log[0]);
            log.resize(static_cast<std::size_t>((std::max)(0, written)));
            return log;
        }

        bool validStage(unsigned int stage) noexcept
        {
            return stage == GL_COMPUTE_SHADER || stage == GL_VERTEX_SHADER ||
                stage == GL_FRAGMENT_SHADER || stage == GL_GEOMETRY_SHADER ||
                stage == GL_TESS_CONTROL_SHADER || stage == GL_TESS_EVALUATION_SHADER;
        }
    }

    struct ParallelShaderCompiler::Impl
    {
        struct Job
        {
            Ticket ticket = 0;
            State state = State::Pending;
            GLuint program = 0;
            std::vector<GLuint> shaders;
            std::string label;
            std::string error;
        };

        std::vector<Job> jobs;
        std::size_t capacity = 0;
        Ticket nextTicket = 1;
        PFNGLMAXSHADERCOMPILERTHREADSARBPROC setThreads = nullptr;
        GLuint previousThreads = 0;
        std::string lastError;
        const std::string empty;

        static void releaseShaders(Job& job) noexcept
        {
            for (const GLuint shader : job.shaders)
            {
                if (job.program) glDetachShader(job.program, shader);
                glDeleteShader(shader);
            }
            job.shaders.clear();
        }

        static void release(Job& job) noexcept
        {
            releaseShaders(job);
            if (job.program) glDeleteProgram(job.program);
            job.program = 0;
        }

        auto find(Ticket ticket) noexcept
        {
            return std::find_if(jobs.begin(), jobs.end(),
                [ticket](const Job& job) { return job.ticket == ticket; });
        }
        auto find(Ticket ticket) const noexcept
        {
            return std::find_if(jobs.begin(), jobs.end(),
                [ticket](const Job& job) { return job.ticket == ticket; });
        }
    };

    ParallelShaderCompiler::ParallelShaderCompiler() : m_impl(new Impl) {}
    ParallelShaderCompiler::~ParallelShaderCompiler() { shutdown(); }

    bool ParallelShaderCompiler::initialize(std::size_t maxJobs, unsigned int compilerThreads)
    {
        shutdown();
        auto& impl = *m_impl;
        impl.lastError.clear();
        if (!maxJobs)
        {
            impl.lastError = "Parallel shader compiler requires nonzero job capacity";
            return false;
        }
        PFNGLMAXSHADERCOMPILERTHREADSARBPROC setThreads = nullptr;
        if (GLEW_ARB_parallel_shader_compile && glMaxShaderCompilerThreadsARB)
            setThreads = glMaxShaderCompilerThreadsARB;
        else if (GLEW_KHR_parallel_shader_compile && glMaxShaderCompilerThreadsKHR)
            setThreads = glMaxShaderCompilerThreadsKHR;
        if (!setThreads)
        {
            impl.lastError = "ARB/KHR_parallel_shader_compile is unavailable; use a linked fallback";
            return false;
        }
        impl.capacity = maxJobs;
        impl.jobs.reserve(maxJobs);
        // Preserve the unsigned 0xFFFFFFFF "implementation maximum" value;
        // a signed 32-bit query can clamp that value on some implementations.
        GLint64 previous = 0;
        glGetInteger64v(GL_MAX_SHADER_COMPILER_THREADS_ARB, &previous);
        impl.previousThreads = static_cast<GLuint>(previous);
        impl.setThreads = setThreads;
        impl.setThreads((std::max)(1u, compilerThreads));
        return true;
    }

    void ParallelShaderCompiler::shutdown() noexcept
    {
        auto& impl = *m_impl;
        for (auto& job : impl.jobs) Impl::release(job);
        impl.jobs.clear();
        if (impl.setThreads) impl.setThreads(impl.previousThreads);
        impl.setThreads = nullptr;
        impl.capacity = 0;
    }

    bool ParallelShaderCompiler::supported() const noexcept { return m_impl->setThreads != nullptr; }

    ParallelShaderCompiler::Ticket ParallelShaderCompiler::submit(
        const std::vector<Source>& sources, const std::string& label, bool retrievableBinary)
    {
        auto& impl = *m_impl;
        if (!supported())
        {
            impl.lastError = "Parallel shader compilation unavailable";
            return 0;
        }
        if (impl.jobs.size() >= impl.capacity)
        {
            impl.lastError = "Parallel shader compiler job capacity reached";
            return 0;
        }
        if (sources.empty() || std::any_of(sources.begin(), sources.end(), [](const Source& source) {
            return !validStage(source.stage) || source.text.empty() ||
                source.text.size() > static_cast<std::size_t>((std::numeric_limits<GLint>::max)());
        }))
        {
            impl.lastError = "Invalid or empty shader source";
            return 0;
        }

        // Finish potentially throwing CPU allocations before creating GL objects.
        Impl::Job job;
        job.shaders.reserve(sources.size());
        job.label = label;
        job.program = glCreateProgram();
        if (!job.program)
        {
            impl.lastError = "Cannot create parallel shader program";
            return 0;
        }
        if (retrievableBinary)
            glProgramParameteri(job.program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        for (const auto& source : sources)
        {
            const GLuint shader = glCreateShader(source.stage);
            if (!shader)
            {
                Impl::release(job);
                impl.lastError = "Cannot create parallel shader stage";
                return 0;
            }
            job.shaders.push_back(shader);
            const GLchar* text = source.text.c_str();
            const GLint length = static_cast<GLint>(source.text.size());
            glShaderSource(shader, 1, &text, &length);
            glCompileShader(shader);
            glAttachShader(job.program, shader);
        }
        glLinkProgram(job.program);
        job.ticket = impl.nextTicket++;
        if (impl.nextTicket == 0) ++impl.nextTicket;
        const auto ticket = job.ticket;
        impl.jobs.push_back(std::move(job));
        impl.lastError.clear();
        return ticket;
    }

    ParallelShaderCompiler::Ticket ParallelShaderCompiler::submitCompute(
        const std::string& source, const std::string& label, bool retrievableBinary)
    {
        return submit({Source{GL_COMPUTE_SHADER, source}}, label, retrievableBinary);
    }

    void ParallelShaderCompiler::poll()
    {
        for (auto& job : m_impl->jobs)
        {
            if (job.state != State::Pending) continue;
            GLint complete = GL_FALSE;
            glGetProgramiv(job.program, GL_COMPLETION_STATUS_ARB, &complete);
            if (complete != GL_TRUE) continue;
            // A failed link need not imply every attached stage has finished.
            // Do not inspect any stage's status/log until all are complete.
            for (const GLuint shader : job.shaders)
            {
                glGetShaderiv(shader, GL_COMPLETION_STATUS_ARB, &complete);
                if (complete != GL_TRUE) break;
            }
            if (complete != GL_TRUE) continue;
            GLint linked = GL_FALSE;
            glGetProgramiv(job.program, GL_LINK_STATUS, &linked);
            if (linked == GL_TRUE)
            {
                Impl::releaseShaders(job);
                job.state = State::Ready;
                continue;
            }
            job.error = "Shader compilation/link failed";
            if (!job.label.empty()) job.error += ": " + job.label;
            for (std::size_t i = 0; i < job.shaders.size(); ++i)
            {
                GLint compiled = GL_FALSE;
                glGetShaderiv(job.shaders[i], GL_COMPILE_STATUS, &compiled);
                if (compiled == GL_FALSE)
                {
                    job.error += "\nStage " + std::to_string(i) + ":\n";
                    job.error += objectLog(job.shaders[i], false);
                }
            }
            const auto linkLog = objectLog(job.program, true);
            if (!linkLog.empty()) job.error += "\nLink:\n" + linkLog;
            Impl::release(job);
            job.state = State::Failed;
        }
    }

    ParallelShaderCompiler::State ParallelShaderCompiler::state(Ticket ticket) const noexcept
    {
        const auto job = m_impl->find(ticket);
        return job == m_impl->jobs.end() ? State::Unknown : job->state;
    }

    const std::string& ParallelShaderCompiler::error(Ticket ticket) const noexcept
    {
        const auto job = m_impl->find(ticket);
        return job == m_impl->jobs.end() ? m_impl->empty : job->error;
    }
    const std::string& ParallelShaderCompiler::lastError() const noexcept { return m_impl->lastError; }
    std::size_t ParallelShaderCompiler::jobCount() const noexcept { return m_impl->jobs.size(); }
    std::size_t ParallelShaderCompiler::pendingCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(m_impl->jobs.begin(), m_impl->jobs.end(),
            [](const Impl::Job& job) { return job.state == State::Pending; }));
    }

    unsigned int ParallelShaderCompiler::takeProgram(Ticket ticket) noexcept
    {
        auto& impl = *m_impl;
        const auto job = impl.find(ticket);
        if (job == impl.jobs.end() || job->state != State::Ready) return 0;
        const GLuint program = job->program;
        job->program = 0;
        impl.jobs.erase(job);
        return program;
    }

    void ParallelShaderCompiler::cancel(Ticket ticket) noexcept
    {
        auto& impl = *m_impl;
        const auto job = impl.find(ticket);
        if (job == impl.jobs.end()) return;
        Impl::release(*job);
        impl.jobs.erase(job);
    }
}
