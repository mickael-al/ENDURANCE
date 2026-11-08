#include "glcore.hpp"
#include "ShaderProgramBatch.hpp"
#include "ParallelShaderCompiler.hpp"
#include "ShaderBinaryCache.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace Ge
{
    namespace
    {
        constexpr std::size_t MaximumPendingPrograms = 8;

        struct OwnedPrograms
        {
            std::vector<unsigned int> ids;
            explicit OwnedPrograms(std::size_t count) : ids(count, 0) {}
            ~OwnedPrograms()
            {
                for (const auto id : ids) if (id) glDeleteProgram(id);
            }
        };

        struct PreparedProgram
        {
            std::vector<ShaderSource> sources;
            std::unique_ptr<detail::ShaderBinaryCache> cache;
        };

        std::string sourceTables(const PreparedProgram& program)
        {
            std::string result;
            for (std::size_t stage = 0; stage < program.sources.size(); ++stage)
            {
                result += "\nStage " + std::to_string(stage) + " GLSL source files (source ID : file):\n";
                const auto& files = program.sources[stage].files;
                for (std::size_t i = 0; i < files.size(); ++i)
                    result += std::to_string(i) + " : " + files[i] + "\n";
            }
            return result;
        }
    }

    std::vector<unsigned int> buildShaderPrograms(
        const std::vector<ShaderProgramRequest>& requests, std::string& error,
        const ShaderCacheOptions& cache, unsigned int compilerThreads)
    {
        error.clear();
        if (requests.empty())
        { error = "Cannot build an empty shader program batch"; return {}; }
        try
        {
            OwnedPrograms output(requests.size());
            ParallelShaderCompiler compiler;
            const auto threads = compilerThreads ? compilerThreads
                : (std::max)(1u, (std::min)(8u, std::thread::hardware_concurrency()));
            const auto capacity = (std::min)(MaximumPendingPrograms, requests.size());
            if (!compiler.initialize(capacity, threads))
            {
                std::cerr << "[Shader batch] Parallel compilation unavailable; building "
                    << requests.size() << " programs during startup" << std::endl;
                for (std::size_t i = 0; i < requests.size(); ++i)
                {
                    const auto& request = requests[i];
                    output.ids[i] = buildShaderProgram(request.stages, error, request.label, cache);
                    if (!output.ids[i]) return {};
                }
                return std::exchange(output.ids, {});
            }

            std::vector<PreparedProgram> prepared(requests.size());
            std::vector<std::size_t> misses;
            misses.reserve(requests.size());
            for (std::size_t i = 0; i < requests.size(); ++i)
            {
                const auto& request = requests[i];
                auto& program = prepared[i];
                if (request.stages.empty())
                {
                    error = "Cannot build a shader program without stages: " + request.label;
                    return {};
                }
                program.sources.resize(request.stages.size());
                for (std::size_t stage = 0; stage < request.stages.size(); ++stage)
                {
                    const auto& file = request.stages[stage];
                    auto& source = program.sources[stage];
                    if (!loadShaderSource(file.filename, file.defines, source, error,
                        file.stage == GL_COMPUTE_SHADER ? "430 core" : "330 core")) return {};
                    if (source.text.size() > static_cast<std::size_t>((std::numeric_limits<GLint>::max)()))
                    { error = "Shader source is too large: " + file.filename; return {}; }
                    if (!request.label.empty())
                        std::cerr << "[Shader " << request.label << "] source " << file.filename
                            << ": " << source.text.size() << " bytes" << std::endl;
                }
                program.cache = std::make_unique<detail::ShaderBinaryCache>(request.stages,
                    program.sources, cache, request.label.empty() ? request.stages.front().filename : request.label);
                output.ids[i] = program.cache->load();
                if (!output.ids[i]) misses.push_back(i);
            }

            std::cerr << "[Shader batch] " << requests.size() - misses.size() << " cache hits; "
                << misses.size() << " compilations, " << capacity << " in-flight programs, "
                << threads << " compiler threads requested" << std::endl;
            struct Pending { std::size_t index; ParallelShaderCompiler::Ticket ticket; };
            std::vector<Pending> pending;
            pending.reserve(capacity);
            std::size_t next = 0;
            while (next < misses.size() || !pending.empty())
            {
                while (next < misses.size() && pending.size() < capacity)
                {
                    const auto index = misses[next++];
                    const auto& request = requests[index];
                    auto& program = prepared[index];
                    std::vector<ParallelShaderCompiler::Source> sources;
                    sources.reserve(request.stages.size());
                    for (std::size_t stage = 0; stage < request.stages.size(); ++stage)
                        sources.push_back({request.stages[stage].stage, program.sources[stage].text});
                    const auto ticket = compiler.submit(sources, request.label, program.cache->available());
                    if (!ticket)
                    {
                        error = "Cannot submit shader program: " + request.label + "\n" + compiler.lastError()
                            + sourceTables(program);
                        return {};
                    }
                    pending.push_back({index, ticket});
                }
                compiler.poll();
                bool progressed = false;
                for (auto it = pending.begin(); it != pending.end();)
                {
                    const auto state = compiler.state(it->ticket);
                    if (state == ParallelShaderCompiler::State::Failed ||
                        state == ParallelShaderCompiler::State::Unknown)
                    {
                        error = compiler.error(it->ticket);
                        if (error.empty()) error = "Lost pending shader program: " + requests[it->index].label;
                        error += sourceTables(prepared[it->index]);
                        return {};
                    }
                    if (state != ParallelShaderCompiler::State::Ready) { ++it; continue; }
                    const auto index = it->index;
                    output.ids[index] = compiler.takeProgram(it->ticket);
                    if (!output.ids[index])
                    { error = "Cannot take completed shader program: " + requests[index].label; return {}; }
                    prepared[index].cache->store(output.ids[index]);
                    it = pending.erase(it);
                    progressed = true;
                }
                // Compilation is a startup gate. Yield briefly while the driver
                // workers progress instead of spinning or issuing glFinish.
                if (!progressed && !pending.empty())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return std::exchange(output.ids, {});
        }
        catch (const std::exception& failure)
        {
            error = std::string("Shader program batch failed: ") + failure.what();
            return {};
        }
    }
}
