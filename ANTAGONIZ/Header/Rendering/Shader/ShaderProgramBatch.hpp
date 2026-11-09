#ifndef ENDURANCE_SHADER_PROGRAM_BATCH_HPP
#define ENDURANCE_SHADER_PROGRAM_BATCH_HPP

#include "ShaderCompilation.hpp"

namespace Ge
{
    struct ShaderProgramRequest
    {
        std::vector<ShaderFile> stages;
        std::string label;
    };

    // Startup/reload operation on the current owning OpenGL context. Reads each
    // distinct include once in a build-local snapshot, captures the binary
    // driver identity once, restores valid binary-cache entries, and compiles only
    // cache misses through the existing driver-assisted parallel compiler. The
    // bounded queue limits in-flight programs independently of the request count.
    // Drivers without ARB/KHR_parallel_shader_compile use the synchronous builder
    // during this startup operation, never a delayed gameplay compilation.
    //
    // Returns owned program IDs in request order; the caller must delete them.
    // On any failure all programs are released and an empty vector plus a full
    // diagnostic is returned. An empty request list is rejected. Zero threads
    // chooses a bounded hardware-concurrency hint; the previous hint is restored.
    std::vector<unsigned int> buildShaderPrograms(
        const std::vector<ShaderProgramRequest>& requests, std::string& error,
        const ShaderCacheOptions& cache = {}, unsigned int compilerThreads = 0);
}

#endif
