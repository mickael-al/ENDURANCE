#include "glcore.hpp"
#include "ShaderCompilation.hpp"
#include "ShaderBinaryCache.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

namespace Ge
{
    namespace
    {
        struct SourceReader
        {
            ShaderSource& source;
            std::string& error;
            std::vector<std::string> active;
            std::size_t versionEnd = std::string::npos;
            unsigned versionNextLine = 1;
            bool versionSeen = false, versionComment = false;

            SourceReader(ShaderSource& output, std::string& diagnostic)
                : source(output), error(diagnostic) {}

            // Track comments so directives mentioned in a comment are inert.
            static std::size_t firstToken(const std::string& line, bool& blockComment)
            {
                std::size_t first = std::string::npos;
                bool quoted = false;
                for (std::size_t i = 0; i < line.size(); ++i)
                {
                    if (blockComment)
                    {
                        if (line.compare(i, 2, "*/") == 0) { blockComment = false; ++i; }
                        continue;
                    }
                    if (quoted)
                    {
                        if (line[i] == '\\') ++i;
                        else if (line[i] == '"') quoted = false;
                        continue;
                    }
                    if (line.compare(i, 2, "//") == 0) break;
                    if (line.compare(i, 2, "/*") == 0) { blockComment = true; ++i; continue; }
                    if (first == std::string::npos && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') first = i;
                    if (line[i] == '"') quoted = true;
                }
                return first;
            }

            static bool directive(const std::string& line, std::size_t first, const char* name)
            {
                if (first == std::string::npos || line[first] != '#') return false;
                first = line.find_first_not_of(" \t", first + 1);
                if (first == std::string::npos) return false;
                const std::string token(name);
                return line.compare(first, token.size(), token) == 0
                    && (first + token.size() == line.size()
                        || line.find_first_of(" \t\r", first + token.size()) == first + token.size());
            }

            bool read(const std::filesystem::path& inputPath, unsigned depth = 0)
            {
                const auto path = inputPath.lexically_normal();
                const auto name = path.generic_string();
                if (depth > 12 || std::find(active.begin(), active.end(), name) != active.end())
                { error = "Recursive shader include: " + name; return false; }
                std::ifstream input(path, std::ios::binary);
                if (!input) { error = "Cannot read shader: " + name; return false; }
                const auto found = std::find(source.files.begin(), source.files.end(), name);
                const auto sourceId = static_cast<unsigned>(found - source.files.begin());
                if (found == source.files.end()) source.files.push_back(name);
                active.push_back(name);
                if (depth) source.text += "#line 1 " + std::to_string(sourceId) + "\n";
                std::string line;
                unsigned lineNumber = 0;
                bool blockComment = false;
                while (std::getline(input, line))
                {
                    ++lineNumber;
                    if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
                    const auto first = firstToken(line, blockComment);
                    if (!depth)
                    {
                        if (!versionSeen && directive(line, first, "version"))
                        {
                            versionSeen = true;
                            versionComment = blockComment;
                            if (!versionComment)
                            { versionEnd = source.text.size() + line.size() + 1; versionNextLine = lineNumber + 1; }
                        }
                        else if (versionComment && (!blockComment || first != std::string::npos))
                        {
                            // Definitions must not land inside a trailing
                            // multiline version comment, even when code follows */.
                            versionEnd = source.text.size() + (first == std::string::npos ? line.size() + 1 : first);
                            versionNextLine = lineNumber + (first == std::string::npos ? 1u : 0u);
                            versionComment = false;
                        }
                    }
                    if (directive(line, first, "include"))
                    {
                        const auto begin = line.find('"', first + 1);
                        const auto end = begin == std::string::npos ? begin : line.find('"', begin + 1);
                        if (begin == std::string::npos || end == std::string::npos || end == begin + 1)
                        { error = "Invalid shader include: " + name + ":" + std::to_string(lineNumber); return false; }
                        // Preserve a leading block-comment terminator, if any.
                        source.text += line.substr(0, first) + "\n";
                        if (!read(path.parent_path() / line.substr(begin + 1, end - begin - 1), depth + 1)) return false;
                        source.text += "#line " + std::to_string(lineNumber) + " " + std::to_string(sourceId) + "\n";
                        source.text += line.substr(end + 1) + "\n";
                    }
                    else
                    {
                        source.text += line + "\n";
                    }
                }
                if (input.bad()) { error = "Cannot finish reading shader: " + name; return false; }
                if (!depth && versionComment)
                { versionEnd = source.text.size(); versionNextLine = lineNumber + 1; }
                active.pop_back();
                return true;
            }
        };

        std::string sourceTable(const ShaderSource& source)
        {
            std::string table = "\nGLSL source files (source ID : file):\n";
            for (std::size_t i = 0; i < source.files.size(); ++i)
                table += std::to_string(i) + " : " + source.files[i] + "\n";
            return table;
        }

        std::string objectLog(GLuint object, bool program)
        {
            GLint length = 0;
            if (program) glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
            else glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>((std::max)(length, 1)), '\0');
            GLsizei written = 0;
            if (program) glGetProgramInfoLog(object, length, &written, &log[0]);
            else glGetShaderInfoLog(object, length, &written, &log[0]);
            log.resize(static_cast<std::size_t>((std::max)(written, 0)));
            return log;
        }

        struct BuildObjects
        {
            std::vector<GLuint> shaders;
            GLuint program = 0;
            ~BuildObjects()
            {
                for (auto shader : shaders) glDeleteShader(shader);
                if (program) glDeleteProgram(program);
            }
        };

        struct BuildTrace
        {
            using Clock = std::chrono::steady_clock;
            const std::string& label;
            template<class Operation> void run(const char* stage, Operation operation) const
            {
                if (label.empty()) { operation(); return; }
                std::cerr << "[Shader " << label << "] " << stage << " begin" << std::endl;
                const auto start = Clock::now();
                operation();
                const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                std::cerr << "[Shader " << label << "] " << stage << " end (" << elapsed << " ms)" << std::endl;
            }
        };
    }

    bool loadShaderSource(const std::string& filename, const std::string& defines,
        ShaderSource& source, std::string& error, const std::string& fallbackVersion)
    {
        source = {};
        error.clear();
        SourceReader reader{source, error};
        if (!reader.read(std::filesystem::path(filename))) return false;
        std::string injected = defines;
        if (!injected.empty() && injected.back() != '\n') injected += '\n';
        injected += "#line " + std::to_string(reader.versionNextLine) + " 0\n";
        if (reader.versionEnd == std::string::npos)
            source.text.insert(0, "#version " + fallbackVersion + "\n" + injected);
        else
        {
            if (reader.versionEnd && source.text[reader.versionEnd - 1] != '\n') injected.insert(0, "\n");
            source.text.insert(reader.versionEnd, injected);
        }
        return true;
    }

    unsigned int buildShaderProgram(const std::vector<ShaderFile>& stages,
        std::string& error, const std::string& traceLabel, const ShaderCacheOptions& cache)
    {
        error.clear();
        if (stages.empty()) { error = "Cannot build a shader program without stages"; return 0; }
        BuildObjects objects;
        objects.shaders.reserve(stages.size());
        std::vector<ShaderSource> sources(stages.size());
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            const auto& stage = stages[i];
            auto& source = sources[i];
            if (!loadShaderSource(stage.filename, stage.defines, source, error,
                stage.stage == GL_COMPUTE_SHADER ? "430 core" : "330 core")) return 0;
            if (source.text.size() > static_cast<std::size_t>((std::numeric_limits<GLint>::max)()))
            { error = "Shader source is too large: " + stage.filename; return 0; }
            const std::string stageLabel = traceLabel.empty() ? std::string{} : traceLabel + " " + stage.filename;
            if (!stageLabel.empty())
                std::cerr << "[Shader " << stageLabel << "] source: " << source.text.size() << " bytes" << std::endl;
        }
        // Includes are always reloaded before hashing: edits and missing files
        // cannot be hidden by a previously cached executable.
        detail::ShaderBinaryCache binaryCache(stages,sources,cache,
            traceLabel.empty() ? stages.front().filename : traceLabel);
        if (const auto program=binaryCache.load()) return program;
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            const auto& stage=stages[i];
            const auto& source=sources[i];
            const std::string stageLabel=traceLabel.empty() ? std::string{} : traceLabel+" "+stage.filename;
            const BuildTrace trace{stageLabel};
            const GLuint shader = glCreateShader(stage.stage);
            if (!shader) { error = "Cannot create shader: " + stage.filename; return 0; }
            objects.shaders.push_back(shader);
            const char* bytes = source.text.data();
            const auto length = static_cast<GLint>(source.text.size());
            glShaderSource(shader, 1, &bytes, &length);
            trace.run("glCompileShader", [&] { glCompileShader(shader); });
            GLint success = GL_FALSE;
            trace.run("GL_COMPILE_STATUS", [&] { glGetShaderiv(shader, GL_COMPILE_STATUS, &success); });
            if (!success)
            { error = "Shader compilation failed: " + stage.filename + "\n" + objectLog(shader, false) + sourceTable(source); return 0; }
        }
        objects.program = glCreateProgram();
        if (!objects.program) { error = "Cannot create shader program: " + stages.front().filename; return 0; }
        for (auto shader : objects.shaders) glAttachShader(objects.program, shader);
        if (binaryCache.available())
            glProgramParameteri(objects.program,GL_PROGRAM_BINARY_RETRIEVABLE_HINT,GL_TRUE);
        const BuildTrace trace{traceLabel};
        trace.run("glLinkProgram", [&] { glLinkProgram(objects.program); });
        GLint success = GL_FALSE;
        trace.run("GL_LINK_STATUS", [&] { glGetProgramiv(objects.program, GL_LINK_STATUS, &success); });
        if (!success)
        {
            error = "Shader program link failed: " + stages.front().filename + "\n" + objectLog(objects.program, true);
            for (const auto& source : sources) error += sourceTable(source);
            return 0;
        }
        binaryCache.store(objects.program);
        for (auto shader : objects.shaders) glDetachShader(objects.program, shader);
        return std::exchange(objects.program, 0u);
    }
}
