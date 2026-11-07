#include "glcore.hpp"
#include "BackgroundShaderCompiler.hpp"

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Ge
{
    namespace
    {
        bool validStage(unsigned int stage) noexcept
        {
            return stage == GL_COMPUTE_SHADER || stage == GL_VERTEX_SHADER ||
                stage == GL_FRAGMENT_SHADER || stage == GL_GEOMETRY_SHADER ||
                stage == GL_TESS_CONTROL_SHADER || stage == GL_TESS_EVALUATION_SHADER;
        }
        std::string objectLog(GLuint object, bool program)
        {
            GLint length = 0;
            if (program) glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
            else glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
            if (length <= 1) return {};
            std::string result(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            if (program) glGetProgramInfoLog(object, length, &written, &result[0]);
            else glGetShaderInfoLog(object, length, &written, &result[0]);
            result.resize(static_cast<std::size_t>((std::max)(0, written)));
            return result;
        }
        struct BuildObjects
        {
            GLuint program = 0;
            std::vector<GLuint> shaders;
            ~BuildObjects()
            {
                for (GLuint shader : shaders) glDeleteShader(shader);
                if (program)
                {
                    glUseProgram(0);
                    glDeleteProgram(program);
                }
            }
        };
    }

    struct BackgroundShaderCompiler::Impl
    {
        struct Job
        {
            Ticket ticket = 0;
            State state = State::Pending;
            bool running = false, cancelled = false;
            GLuint program = 0;
            std::vector<Source> sources;
            std::string label, error;
            PrepareProgram prepare;
        };
        mutable std::mutex mutex;
        std::condition_variable wake, startup;
        std::thread worker;
        ContextHooks hooks;
        std::vector<std::shared_ptr<Job>> jobs;
        std::size_t capacity = 0;
        Ticket nextTicket = 1;
        bool stopping = false, started = false, available = false;
        std::string lastError;

        auto find(Ticket ticket) const noexcept
        {
            return std::find_if(jobs.begin(), jobs.end(),
                [ticket](const std::shared_ptr<Job>& job) { return job->ticket == ticket; });
        }
        bool wasCancelled(const Job& job)
        {
            std::lock_guard<std::mutex> lock(mutex);
            return stopping || job.cancelled;
        }
        GLuint build(const std::shared_ptr<Job>& job)
        {
            BuildObjects objects;
            objects.shaders.reserve(job->sources.size());
            objects.program = glCreateProgram();
            if (!objects.program) throw std::runtime_error("Cannot create shader program");
            for (std::size_t i = 0; i < job->sources.size(); ++i)
            {
                if (wasCancelled(*job)) return 0;
                const auto& source = job->sources[i];
                const GLuint shader = glCreateShader(source.stage);
                if (!shader) throw std::runtime_error("Cannot create shader stage");
                objects.shaders.push_back(shader);
                const GLchar* text = source.text.c_str();
                const GLint length = static_cast<GLint>(source.text.size());
                glShaderSource(shader, 1, &text, &length);
                glCompileShader(shader);
                GLint compiled = GL_FALSE;
                // This deliberately blocking query is confined to the worker.
                glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
                if (compiled != GL_TRUE)
                    throw std::runtime_error("Stage " + std::to_string(i) + ":\n" + objectLog(shader, false));
                glAttachShader(objects.program, shader);
            }
            if (wasCancelled(*job)) return 0;
            glLinkProgram(objects.program);
            GLint linked = GL_FALSE;
            glGetProgramiv(objects.program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE) throw std::runtime_error("Link:\n" + objectLog(objects.program, true));
            for (GLuint shader : objects.shaders)
            {
                glDetachShader(objects.program, shader);
                glDeleteShader(shader);
            }
            objects.shaders.clear();
            if (wasCancelled(*job)) return 0;
            if (job->prepare) job->prepare(objects.program);
            // Do not retain a transferred/deleted program as current in the
            // worker context after preparation or warm-up.
            glUseProgram(0);
            // Shared objects must be complete before another context consumes
            // them. This wait is on this dedicated worker only, whose context
            // carries compilation/preparation commands, never frame rendering.
            glFinish();
            if (wasCancelled(*job)) return 0;
            const GLuint result = objects.program;
            objects.program = 0;
            return result;
        }
        bool hasWork() const noexcept
        {
            return stopping || std::any_of(jobs.begin(), jobs.end(), [](const auto& job) {
                return job->cancelled || job->state == State::Pending;
            });
        }
        void runJobs()
        {
            for (;;)
            {
                std::shared_ptr<Job> job;
                bool discard = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [this] { return hasWork(); });
                    // Cancelled/completed objects are reaped before new driver
                    // work, keeping the queue and ownership strictly bounded.
                    auto found = std::find_if(jobs.begin(), jobs.end(),
                        [this](const auto& candidate) { return stopping || candidate->cancelled; });
                    if (found != jobs.end())
                    {
                        job = *found;
                        jobs.erase(found);
                        discard = true;
                    }
                    else if (stopping) return;
                    else
                    {
                        found = std::find_if(jobs.begin(), jobs.end(),
                            [](const auto& candidate) { return candidate->state == State::Pending; });
                        if (found == jobs.end()) continue;
                        job = *found;
                        job->running = true;
                    }
                }
                if (discard)
                {
                    if (job->program) glDeleteProgram(job->program);
                    continue;
                }
                GLuint program = 0;
                std::string failure;
                try { program = build(job); }
                catch (const std::exception& error)
                { failure = job->label.empty() ? error.what() : job->label + ":\n" + error.what(); }
                catch (...) { failure = "Unknown background shader compilation/preparation failure"; }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    job->running = false;
                    discard = stopping || job->cancelled;
                    if (discard)
                    {
                        const auto found = find(job->ticket);
                        if (found != jobs.end()) jobs.erase(found);
                    }
                    else
                    {
                        job->program = program;
                        job->error = std::move(failure);
                        job->state = program ? State::Ready : State::Failed;
                    }
                }
                if (discard && program) glDeleteProgram(program);
                // Expanded shader sources can be large; free them on the worker
                // as soon as linking is done, without holding the consumer mutex.
                job->sources.clear();
                job->prepare = {};
            }
        }
        void run() noexcept
        {
            bool current = false;
            std::string failure;
            try
            {
                current = hooks.makeCurrent();
                if (!current) failure = "Cannot make background shader context current";
            }
            catch (const std::exception& error) { failure = error.what(); }
            catch (...) { failure = "Background shader context activation threw an exception"; }
            {
                std::lock_guard<std::mutex> lock(mutex);
                available = current;
                started = true;
                if (!failure.empty()) lastError = std::move(failure);
            }
            startup.notify_one();
            if (current)
            {
                try { runJobs(); }
                catch (...)
                {
                    // Exceptional worker failure must never terminate the process
                    // or leave the owner waiting indefinitely on Pending tickets.
                    // Do not hold the consumer mutex over even cleanup GL calls.
                    for (;;)
                    {
                        GLuint program = 0;
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            available = false;
                            const auto found = std::find_if(jobs.begin(), jobs.end(),
                                [](const auto& job) { return job->program != 0; });
                            if (found == jobs.end()) break;
                            program = (*found)->program;
                            (*found)->program = 0;
                        }
                        glDeleteProgram(program);
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        lastError = "Background shader compiler worker stopped unexpectedly";
                        for (auto& job : jobs)
                        {
                            job->error = lastError;
                            job->state = State::Failed;
                        }
                    }
                }
                try { hooks.releaseCurrent(); }
                catch (...) {} // Context destruction remains the caller's responsibility.
            }
        }
    };

    BackgroundShaderCompiler::BackgroundShaderCompiler() : m_impl(new Impl) {}
    BackgroundShaderCompiler::~BackgroundShaderCompiler() { shutdown(); }

    bool BackgroundShaderCompiler::initialize(ContextHooks hooks, std::size_t maxJobs)
    {
        shutdown();
        auto& impl = *m_impl;
        if (!hooks.makeCurrent || !hooks.releaseCurrent || !maxJobs)
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.lastError = "Background compiler requires context hooks and nonzero capacity";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.hooks = std::move(hooks);
            impl.capacity = maxJobs;
            impl.jobs.reserve(maxJobs);
            impl.stopping = impl.started = impl.available = false;
            impl.lastError.clear();
        }
        try { impl.worker = std::thread([&impl] { impl.run(); }); }
        catch (const std::exception& error)
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.lastError = error.what();
            impl.hooks = {};
            return false;
        }
        std::unique_lock<std::mutex> lock(impl.mutex);
        impl.startup.wait(lock, [&impl] { return impl.started; });
        const bool ready = impl.available;
        lock.unlock();
        if (!ready)
        {
            impl.worker.join();
            std::lock_guard<std::mutex> cleanup(impl.mutex);
            impl.hooks = {};
        }
        return ready;
    }

    void BackgroundShaderCompiler::shutdown() noexcept
    {
        auto& impl = *m_impl;
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.stopping = true;
            impl.available = false;
        }
        impl.wake.notify_one();
        if (impl.worker.joinable()) impl.worker.join();
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.jobs.clear();
            impl.capacity = 0;
            impl.hooks = {};
        }
    }

    bool BackgroundShaderCompiler::supported() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->available && !m_impl->stopping;
    }

    BackgroundShaderCompiler::Ticket BackgroundShaderCompiler::submit(
        std::vector<Source> sources, std::string label, PrepareProgram prepare)
    {
        if (sources.empty() || std::any_of(sources.begin(), sources.end(), [](const Source& source) {
            return !validStage(source.stage) || source.text.empty() ||
                source.text.size() > static_cast<std::size_t>((std::numeric_limits<GLint>::max)());
        }))
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            m_impl->lastError = "Invalid or empty shader source";
            return 0;
        }
        auto job = std::make_shared<Impl::Job>();
        job->sources = std::move(sources);
        job->label = std::move(label);
        job->prepare = std::move(prepare);
        Ticket ticket = 0;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            auto& impl = *m_impl;
            if (!impl.available || impl.stopping)
            {
                impl.lastError = "Background shader context unavailable";
                return 0;
            }
            if (impl.jobs.size() >= impl.capacity)
            {
                impl.lastError = "Background shader compiler job capacity reached";
                return 0;
            }
            ticket = job->ticket = impl.nextTicket++;
            if (!impl.nextTicket) ++impl.nextTicket;
            impl.jobs.push_back(std::move(job));
            impl.lastError.clear();
        }
        m_impl->wake.notify_one();
        return ticket;
    }

    BackgroundShaderCompiler::Ticket BackgroundShaderCompiler::submitCompute(
        std::string source, std::string label, PrepareProgram prepare)
    {
        std::vector<Source> sources;
        sources.push_back({GL_COMPUTE_SHADER, std::move(source)});
        return submit(std::move(sources), std::move(label), std::move(prepare));
    }

    void BackgroundShaderCompiler::poll() noexcept {}

    BackgroundShaderCompiler::State BackgroundShaderCompiler::state(Ticket ticket) const noexcept
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        const auto job = m_impl->find(ticket);
        return job == m_impl->jobs.end() || (*job)->cancelled ? State::Unknown : (*job)->state;
    }
    std::string BackgroundShaderCompiler::error(Ticket ticket) const
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        const auto job = m_impl->find(ticket);
        return job == m_impl->jobs.end() ? std::string{} : (*job)->error;
    }
    std::string BackgroundShaderCompiler::lastError() const
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->lastError;
    }
    std::size_t BackgroundShaderCompiler::jobCount() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->jobs.size();
    }
    std::size_t BackgroundShaderCompiler::pendingCount() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return static_cast<std::size_t>(std::count_if(m_impl->jobs.begin(), m_impl->jobs.end(),
            [](const auto& job) { return !job->cancelled && job->state == State::Pending; }));
    }

    unsigned int BackgroundShaderCompiler::takeProgram(Ticket ticket) noexcept
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        const auto job = m_impl->find(ticket);
        if (job == m_impl->jobs.end() || (*job)->cancelled || (*job)->state != State::Ready) return 0;
        const GLuint program = (*job)->program;
        (*job)->program = 0;
        m_impl->jobs.erase(job);
        return program;
    }
    void BackgroundShaderCompiler::cancel(Ticket ticket) noexcept
    {
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            const auto job = m_impl->find(ticket);
            if (job != m_impl->jobs.end()) (*job)->cancelled = true;
        }
        m_impl->wake.notify_one();
    }
}
