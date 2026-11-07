// Runs against a temporary writable copy of the production shaders. It proves
// that a nested biome edit changes real meshed vertices after reinitialization.
#include "glcore.hpp"
#include "ProceduralTerrainGpu.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace ProceduralTerrain;
namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "Cannot read test shader " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& source)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << source;
    require(file.good(), "Cannot write test shader " + path.string());
}
std::string planeRelief(std::string source, const char* height)
{
    const auto function = source.find("vec4 fieldReliefPolarIce(");
    require(function != std::string::npos, "Production biome relief entry point missing");
    const auto body = source.find('{', function);
    require(body != std::string::npos, "Production biome relief body missing");
    source.insert(body + 1, std::string("\n    return vec4(") + height + ", 0.0, 0.0, 0.0);\n");
    return source;
}
MeshHandle finishPlane(GpuMesher& mesher, float height, std::uint64_t token)
{
    ChunkRequest request;
    require(mesher.submit(request, token), mesher.lastError());
    std::vector<Completion> completed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (completed.empty() && std::chrono::steady_clock::now() < deadline)
    {
        mesher.poll(completed);
        std::this_thread::yield();
    }
    require(completed.size() == 1 && completed.front().status == CompletionStatus::Ready,
        "Reload did not generate its plane: " + mesher.lastError());
    const auto mesh = completed.front().mesh;
    require(mesh.vertexCount == 384, "Reloaded biome plane triangle count changed");
    std::vector<std::uint32_t> packed(mesh.vertexCount * 3);
    glGetNamedBufferSubData(mesher.vertexBuffer(), static_cast<GLintptr>(mesh.firstVertex) * 12,
        static_cast<GLsizeiptr>(packed.size() * sizeof(packed.front())), packed.data());
    for (std::size_t vertex = 0; vertex < mesh.vertexCount; ++vertex)
    {
        const float y = static_cast<float>(packed[vertex * 3] >> 16u) * (mesh.extent / 65534.0f);
        require(std::abs(y - height) < 0.001f, "Nested biome source edit did not reach meshed vertices");
    }
    require(glGetError() == GL_NO_ERROR, "OpenGL error during shader reload");
    return mesh;
}
}

extern "C" int terrain_reload_tests(const char* shaderDirectory)
{
    try
    {
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        require(status == GLEW_OK || status == GLEW_ERROR_NO_GLX_DISPLAY, "GLEW initialization failed");
        require(GLEW_VERSION_4_5 && glCreateBuffers, "Reload test requires desktop OpenGL 4.5");
        while (glGetError() != GL_NO_ERROR) {}
        const std::filesystem::path directory(shaderDirectory);
        const auto reliefPath = directory / "biome_relief.glsl";
        const auto volumePath = directory / "biome_volume.glsl";
        const auto originalRelief = read(reliefPath);
        const auto originalVolume = read(volumePath);
        GpuConfig config;
        config.cells = 8; config.maxInFlight = 1; config.maxResidentChunks = 2;
        config.vertexBudgetMiB = 1; config.shaderDirectory = directory.string();
        config.field.heightAmplitude = 0; config.field.detailScale = 1;
        config.field.bandThickness = 0; config.field.bandVariation = 0;
        config.field.bandOffsetVariation = 0; config.field.biomeOverride = 0;
        GpuMesher mesher;
        std::string error;

        write(reliefPath, planeRelief(originalRelief, "2.25"));
        require(mesher.initialize(config, error), error);
        const auto first = finishPlane(mesher, 2.25f, 1);
        write(reliefPath, planeRelief(originalRelief, "6.25"));
        require(mesher.initialize(config, error), error);
        const auto second = finishPlane(mesher, 6.25f, 2);
        require(!mesher.valid(first) && mesher.valid(second), "Reload retained an obsolete mesh handle");

        const auto invalidLine = std::count(originalVolume.begin(), originalVolume.end(), '\n') + 2;
        write(volumePath, originalVolume + "\nPOLAR_RELOAD_SYNTAX_ERROR\n");
        require(!mesher.initialize(config, error), "Invalid nested biome GLSL unexpectedly compiled");
        require(error.find("biome_volume.glsl") != std::string::npos &&
            error.find("GLSL source files") != std::string::npos &&
            error.find(std::to_string(invalidLine)) != std::string::npos,
            "Compiler error did not identify the nested source and original line: " + error);
        require(mesher.stats().allocatedBytes == 0 && !mesher.valid(second),
            "Failed recompilation retained stale allocations");
        write(volumePath, originalVolume);
        require(mesher.initialize(config, error), error);
        finishPlane(mesher, 6.25f, 3);

        std::filesystem::rename(volumePath, directory / "biome_volume.saved");
        require(!mesher.initialize(config, error) && error.find("biome_volume.glsl") != std::string::npos,
            "Missing nested include did not report its path");
        std::filesystem::rename(directory / "biome_volume.saved", volumePath);
        require(mesher.initialize(config, error), error);
        finishPlane(mesher, 6.25f, 4);
        mesher.shutdown();
        require(glGetError() == GL_NO_ERROR, "OpenGL error after failed/recovered reload");
        std::cout << "PASS shader reload: nested biome edit changes real vertices without process restart\n";
        std::cout << "PASS shader diagnostics: original include path/line, missing file and corrected-file retry\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL shader reload: " << error.what() << '\n';
        return 1;
    }
}
