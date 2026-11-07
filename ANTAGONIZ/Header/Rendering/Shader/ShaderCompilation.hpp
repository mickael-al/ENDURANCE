#ifndef ENDURANCE_SHADER_COMPILATION_HPP
#define ENDURANCE_SHADER_COMPILATION_HPP

#include <string>
#include <vector>

namespace Ge
{
    struct ShaderSource
    {
        std::string text;
        std::vector<std::string> files; // Numeric GLSL #line source IDs.
    };

    // Reloads every quoted, relative include from disk. No source/program cache.
    // Definitions follow #version; source IDs and original line numbers survive.
    bool loadShaderSource(const std::string& filename, const std::string& defines,
        ShaderSource& source, std::string& error, const std::string& fallbackVersion = "330 core");

    struct ShaderFile
    {
        unsigned int stage = 0; // GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER, ...
        std::string filename;
        std::string defines;
    };

    // Synchronous, on the owning current OpenGL context. Returns an owned linked
    // program, or zero with a full diagnostic. All temporary shaders are deleted.
    // An optional label enables flushed startup traces, never per-frame work.
    unsigned int buildShaderProgram(const std::vector<ShaderFile>& stages,
        std::string& error, const std::string& traceLabel = {});
}

#endif
