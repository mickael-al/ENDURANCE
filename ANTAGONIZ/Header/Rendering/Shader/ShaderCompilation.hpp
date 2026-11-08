#ifndef ENDURANCE_SHADER_COMPILATION_HPP
#define ENDURANCE_SHADER_COMPILATION_HPP

#include <string>
#include <unordered_map>
#include <vector>

namespace Ge
{
    struct ShaderSource
    {
        std::string text;
        std::vector<std::string> files; // Numeric GLSL #line source IDs.
    };

    // One explicit startup/reload snapshot. Includes shared by many programs
    // are opened once, while each expansion retains its own numeric #line IDs.
    // A new build creates a new snapshot; no file timestamps or process-wide
    // cache can hide an edit. Used only by the thread preparing this build.
    class ShaderSourceSnapshot
    {
    public:
        const std::vector<std::string>* read(const std::string& filename, std::string& error);
        std::size_t fileCount() const noexcept { return m_files.size(); }
    private:
        std::unordered_map<std::string, std::vector<std::string>> m_files;
    };

    // Reloads every quoted, relative include, including on binary-cache hits.
    // An optional build-local snapshot shares disk reads across related programs.
    // Definitions follow #version; source IDs and original line numbers survive.
    bool loadShaderSource(const std::string& filename, const std::string& defines,
        ShaderSource& source, std::string& error, const std::string& fallbackVersion = "330 core",
        ShaderSourceSnapshot* snapshot = nullptr);

    struct ShaderFile
    {
        unsigned int stage = 0; // GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER, ...
        std::string filename;
        std::string defines;
    };

    struct ShaderCacheOptions
    {
        bool enabled = false;
        // Optional UTF-8 directory. Otherwise ENDURANCE_SHADER_CACHE_DIR or
        // the current user's platform cache directory is used.
        std::string directory;
    };

    // Synchronous, on the owning current OpenGL context. Returns an owned linked
    // program, or zero with a full diagnostic. All temporary shaders are deleted.
    // An optional label enables flushed startup traces, never per-frame work.
    unsigned int buildShaderProgram(const std::vector<ShaderFile>& stages,
        std::string& error, const std::string& traceLabel = {}, const ShaderCacheOptions& cache = {});

    namespace detail
    {
        class ShaderBinaryCache;
        // Shared synchronous compiler for prepared batch misses and standalone
        // builds. Sources and binary identity have already been validated.
        unsigned int buildPreparedShaderProgram(const std::vector<ShaderFile>& stages,
            const std::vector<ShaderSource>& sources, std::string& error,
            const std::string& traceLabel, const ShaderBinaryCache& binaryCache);
    }
}

#endif
