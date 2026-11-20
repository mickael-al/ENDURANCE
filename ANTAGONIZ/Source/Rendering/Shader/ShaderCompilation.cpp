#include "glcore.hpp"
#include "ShaderCompilation.hpp"
#include "ShaderBinaryCache.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Ge
{
    namespace
    {
        struct SourceReader
        {
            ShaderSource& source;
            std::string& error;
            ShaderSourceSnapshot& snapshot;
            std::vector<std::string> active;
            std::size_t versionEnd = std::string::npos;
            unsigned versionNextLine = 1;
            bool versionSeen = false, versionComment = false;

            SourceReader(ShaderSource& output, std::string& diagnostic, ShaderSourceSnapshot& files)
                : source(output), error(diagnostic), snapshot(files) {}

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
                const auto* lines = snapshot.read(name, error);
                if (!lines) return false;
                const auto found = std::find(source.files.begin(), source.files.end(), name);
                const auto sourceId = static_cast<unsigned>(found - source.files.begin());
                if (found == source.files.end()) source.files.push_back(name);
                active.push_back(name);
                if (depth) source.text += "#line 1 " + std::to_string(sourceId) + "\n";
                unsigned lineNumber = 0;
                bool blockComment = false;
                for (const auto& line : *lines)
                {
                    ++lineNumber;
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

        struct SourceLocation { unsigned file = 0, line = 0; };

        bool sourceNumber(const std::string& text, std::size_t& position, unsigned& result)
        {
            const auto begin = position;
            result = 0;
            while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            {
                const unsigned digit = static_cast<unsigned>(text[position++] - '0');
                if (result > ((std::numeric_limits<unsigned>::max)() - digit) / 10u) return false;
                result = result * 10u + digit;
            }
            return position != begin;
        }

        bool diagnosticLocation(const std::string& line, SourceLocation& location)
        {
            auto position = line.find_first_not_of(" \t\r");
            if (position == std::string::npos) return false;
            for (const auto* prefix : {"ERROR:", "WARNING:"})
            {
                if (line.compare(position, std::char_traits<char>::length(prefix), prefix) == 0)
                {
                    position = line.find_first_not_of(" \t", position + std::char_traits<char>::length(prefix));
                    break;
                }
            }
            if (position == std::string::npos || !sourceNumber(line, position, location.file)
                || position >= line.size()) return false;
            const char separator = line[position++];
            if ((separator != '(' && separator != ':') || !sourceNumber(line, position, location.line)
                || position >= line.size() || !location.line) return false;
            // NVIDIA: source(line). Mesa/AMD: source:line(column) or source:line:.
            return separator == '(' ? line[position] == ')'
                : line[position] == '(' || line[position] == ':';
        }

        std::string sourceContext(const ShaderSource& source, const std::string& diagnostic)
        {
            // Failure-only work: use the exact expanded snapshot, never reread
            // files that an editor may have changed while the driver compiled.
            std::vector<SourceLocation> locations;
            std::istringstream messages(diagnostic);
            std::string line;
            while (locations.size() < 2 && std::getline(messages, line))
            {
                SourceLocation location;
                if (!diagnosticLocation(line, location) || location.file >= source.files.size()) continue;
                const auto same = [&](const SourceLocation& other)
                { return other.file == location.file && other.line == location.line; };
                if (std::find_if(locations.begin(), locations.end(), same) == locations.end()) locations.push_back(location);
            }
            if (locations.empty()) return {};
            struct OriginalLine { SourceLocation at; std::string text; bool injected = false; };
            std::vector<OriginalLine> context;
            std::unordered_map<std::string, std::vector<OriginalLine>> macros;
            std::istringstream expanded(source.text);
            SourceLocation at{0, 1};
            bool blockComment = false;
            bool sourceMapped = false;
            while (std::getline(expanded, line))
            {
                const auto token = SourceReader::firstToken(line, blockComment);
                if (SourceReader::directive(line, token, "line"))
                {
                    std::size_t position = line.find_first_not_of(" \t", line.find("line", token) + 4);
                    unsigned number = 0;
                    if (position != std::string::npos && sourceNumber(line, position, number))
                    {
                        sourceMapped = true;
                        at.line = number;
                        position = line.find_first_not_of(" \t", position);
                        if (position != std::string::npos && sourceNumber(line, position, number)) at.file = number;
                        continue;
                    }
                }
                if (at.file < source.files.size())
                {
                    for (const auto& target : locations)
                    {
                        const auto distance = at.line > target.line ? at.line - target.line : target.line - at.line;
                        if (at.file != target.file || distance > 3u) continue;
                        const auto same = [&](const OriginalLine& other)
                        { return other.at.file == at.file && other.at.line == at.line; };
                        const auto found = std::find_if(context.begin(), context.end(), same);
                        if (found == context.end()) context.push_back({at, line});
                        else found->text = line;
                        break;
                    }
                    if (SourceReader::directive(line, token, "define"))
                    {
                        auto name = line.find_first_not_of(" \t", line.find("define", token) + 6);
                        if (name != std::string::npos)
                        {
                            auto end = line.find_first_of(" \t(\r", name);
                            const auto key = line.substr(name, end == std::string::npos ? end : end - name);
                            auto& candidates = macros[key];
                            if (candidates.size() < 4 && std::none_of(candidates.begin(), candidates.end(),
                                [&](const OriginalLine& item) { return item.at.file == at.file && item.at.line == at.line; }))
                                candidates.push_back({at, line, !sourceMapped});
                        }
                    }
                }
                if (at.line != (std::numeric_limits<unsigned>::max)()) ++at.line;
            }
            std::string result = "\nGLSL source context (snapshot before preprocessing):\n";
            std::vector<std::pair<std::string, unsigned>> references;
            const auto identifiers = [&](const std::string& text, unsigned depth)
            {
                const auto start = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
                for (std::size_t i = 0; i < text.size() && references.size() < 256;)
                {
                    if (!start(text[i])) { ++i; continue; }
                    const auto begin = i++;
                    while (i < text.size() && (start(text[i]) || (text[i] >= '0' && text[i] <= '9'))) ++i;
                    references.emplace_back(text.substr(begin, i - begin), depth);
                }
            };
            for (const auto& location : locations)
            {
                result += source.files[location.file] + ":" + std::to_string(location.line) + "\n";
                for (const auto& original : context)
                {
                    const auto distance = original.at.line > location.line ? original.at.line - location.line : location.line - original.at.line;
                    if (original.at.file != location.file || distance > 3u) continue;
                    const bool reported = original.at.line == location.line;
                    result += reported ? " > " : "   ";
                    result += std::to_string(original.at.line) + " | " + original.text.substr(0, 240) + "\n";
                    if (reported) identifiers(original.text, 0);
                }
            }
            std::unordered_set<std::string> visited;
            unsigned shown = 0;
            for (std::size_t i = 0; i < references.size() && shown < 12; ++i)
            {
                const auto reference = references[i];
                if (!visited.insert(reference.first).second) continue;
                const auto found = macros.find(reference.first);
                if (found == macros.end()) continue;
                if (!shown) result += "Referenced macro definitions (conditional definitions may be inactive):\n";
                for (const auto& definition : found->second)
                {
                    result += definition.injected ? "   injected shader definition"
                        : "   " + source.files[definition.at.file] + ":" + std::to_string(definition.at.line);
                    result += " | " + definition.text.substr(0, 240) + "\n";
                    if (reference.second < 2) identifiers(definition.text, reference.second + 1);
                    if (++shown == 12) break;
                }
            }
            return result;
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

    std::string detail::shaderSourceContext(const ShaderSource& source, const std::string& diagnostic)
    {
        return sourceContext(source, diagnostic);
    }

    const std::vector<std::string>* ShaderSourceSnapshot::read(const std::string& filename,
        std::string& error)
    {
        const auto found = m_files.find(filename);
        if (found != m_files.end()) return &found->second;
        std::ifstream input(std::filesystem::path(filename), std::ios::binary);
        if (!input) { error = "Cannot read shader: " + filename; return nullptr; }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line))
        {
            if (lines.empty() && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            lines.push_back(std::move(line));
        }
        if (input.bad()) { error = "Cannot finish reading shader: " + filename; return nullptr; }
        return &m_files.emplace(filename, std::move(lines)).first->second;
    }

    bool loadShaderSource(const std::string& filename, const std::string& defines,
        ShaderSource& source, std::string& error, const std::string& fallbackVersion,
        ShaderSourceSnapshot* snapshot)
    {
        source = {};
        error.clear();
        ShaderSourceSnapshot localFiles;
        SourceReader reader{source, error, snapshot ? *snapshot : localFiles};
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
        ShaderSourceSnapshot snapshot;
        std::vector<ShaderSource> sources(stages.size());
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            const auto& stage = stages[i];
            auto& source = sources[i];
            if (!loadShaderSource(stage.filename, stage.defines, source, error,
                stage.stage == GL_COMPUTE_SHADER ? "430 core" : "330 core", &snapshot)) return 0;
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
        return detail::buildPreparedShaderProgram(stages, sources, error, traceLabel, binaryCache);
    }

    unsigned int detail::buildPreparedShaderProgram(const std::vector<ShaderFile>& stages,
        const std::vector<ShaderSource>& sources, std::string& error,
        const std::string& traceLabel, const ShaderBinaryCache& binaryCache)
    {
        BuildObjects objects;
        objects.shaders.reserve(stages.size());
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
            {
                const auto log = objectLog(shader, false);
                error = "Shader compilation failed: " + stage.filename + "\n" + log
                    + sourceContext(source, log) + sourceTable(source);
                return 0;
            }
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
