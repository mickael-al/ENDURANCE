// Runs against a temporary writable copy of the production shaders. It proves
// that a merged biome entry edit changes real meshed vertices after reinitialization.
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
unsigned compileCalls=0, linkCalls=0, binaryCalls=0;
PFNGLCOMPILESHADERPROC originalCompile=nullptr;
PFNGLLINKPROGRAMPROC originalLink=nullptr;
PFNGLPROGRAMBINARYPROC originalBinary=nullptr;
void GLAPIENTRY observeCompile(GLuint shader) { ++compileCalls; originalCompile(shader); }
void GLAPIENTRY observeLink(GLuint program) { ++linkCalls; originalLink(program); }
void GLAPIENTRY observeBinary(GLuint program,GLenum format,const void* binary,GLsizei size)
{ ++binaryCalls; originalBinary(program,format,binary,size); }
struct CompileObserver
{
    CompileObserver()
    {
        compileCalls=linkCalls=binaryCalls=0;
        originalCompile=__glewCompileShader; originalLink=__glewLinkProgram;
        originalBinary=__glewProgramBinary;
        __glewCompileShader=observeCompile; __glewLinkProgram=observeLink;
        if(originalBinary) __glewProgramBinary=observeBinary;
    }
    ~CompileObserver()
    {
        __glewCompileShader=originalCompile; __glewLinkProgram=originalLink;
        __glewProgramBinary=originalBinary;
    }
};
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
std::string planeRelief(std::string source, const char* height, const char* thickness="0.0")
{
    const auto function = source.find("vec4 fieldRelief(");
    require(function != std::string::npos, "Production biome relief entry point missing");
    const auto body = source.find('{', function);
    require(body != std::string::npos, "Production biome relief body missing");
    source.insert(body + 1, std::string("\n    return vec4(") + height + ", " + thickness + ", 0.0, 0.0);\n");
    return source;
}
std::string constantDelta(std::string source,const char* delta)
{
    const auto function = source.find("float fieldVolume(");
    require(function != std::string::npos, "Independent polar volume function missing");
    const auto body = source.find('{', function);
    require(body != std::string::npos, "Production volume evaluator body missing");
    // A completely custom function proves the per-biome API is a density
    // contribution, rather than an entry in a shared parameter interpolation.
    source.insert(body + 1, std::string("\n    return ")+delta+";\n");
    return source;
}
MeshHandle finishPlane(GpuMesher& mesher, float height, std::uint64_t token)
{
    ChunkRequest request;
    require(mesher.submit(request, token), mesher.lastError());
    std::vector<Completion> completed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
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
        const auto reliefPath = directory / "biomes/PolarIce/relief.C.glsl";
        const auto volumePath = directory / "biomes/PolarIce/volume.C.glsl";
        const auto originalRelief = read(reliefPath);
        const auto originalVolume = read(volumePath);
        const auto noise2Path = directory / "field_noise_2d.glsl";
        const auto noise3Path = directory / "field_noise_3d.glsl";
        const auto originalNoise2 = read(noise2Path);
        const auto originalNoise3 = read(noise3Path);
        GpuConfig config;
        config.cells = 8; config.maxInFlight = 1; config.maxResidentChunks = 2;
        config.vertexBudgetMiB = 1; config.shaderDirectory = directory.string();
        config.field.heightAmplitude = 0; config.field.detailScale = 1;
        config.field.bandOffsetVariation = 0; config.field.biomeOverride = 0;
        GpuMesher mesher;
        std::string error;
        GLint binaryFormats=0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS,&binaryFormats);
        const bool binariesSupported=binaryFormats>0 && glProgramBinary && glGetProgramBinary;
        CompileObserver observer;
        constexpr unsigned programs=60; // 26 2D + 26 3D + 4 field + 4 extraction.

        write(reliefPath, planeRelief(originalRelief, "2.25"));
        require(mesher.initialize(config, error), error);
        const auto first = finishPlane(mesher, 2.25f, 1);
        const auto coldCompiles=compileCalls, coldLinks=linkCalls, coldBinaries=binaryCalls;
        require(coldCompiles==programs && coldLinks==programs,"Cold terrain initialization did not compile the fixed 60-program family");
        require(mesher.initialize(config,error),error);
        finishPlane(mesher,2.25f,10);
        if(binariesSupported)
            require(compileCalls==coldCompiles && linkCalls==coldLinks && binaryCalls==coldBinaries+programs,
                "Unchanged regeneration did not load the full fixed family from cache");
        // Settings are uniforms, not source changes. A binary load resets
        // uniforms: this also checks that reinitialization reapplies them.
        config.field.baseHeight=1.0f;
        const auto warmCompiles=compileCalls, warmLinks=linkCalls, warmBinaries=binaryCalls;
        require(mesher.initialize(config,error),error);
        finishPlane(mesher,3.25f,11);
        if(binariesSupported)
            require(compileCalls==warmCompiles && linkCalls==warmLinks && binaryCalls==warmBinaries+programs,
                "Changing terrain settings unnecessarily recompiled cached programs");
        config.field.baseHeight=0;
        const auto editCompiles=compileCalls, editLinks=linkCalls, editBinaries=binaryCalls;
        write(reliefPath, planeRelief(originalRelief, "6.25"));
        require(mesher.initialize(config, error), error);
        const auto second = finishPlane(mesher, 6.25f, 2);
        if(binariesSupported)
            require(compileCalls==editCompiles+1 && linkCalls==editLinks+1 && binaryCalls==editBinaries+programs-1,
                "Polar relief edit must rebuild only its 2D specialization");
        require(!mesher.valid(first) && mesher.valid(second), "Reload retained an obsolete mesh handle");

        auto isolatedReload = [&](const char* label, float expectedHeight, std::uint64_t token,unsigned expectedCompiles=1)
        {
            const auto beforeCompiles=compileCalls, beforeLinks=linkCalls, beforeBinaries=binaryCalls;
            require(mesher.initialize(config,error),error);
            finishPlane(mesher,expectedHeight,token);
            if(binariesSupported)
                require(compileCalls==beforeCompiles+expectedCompiles && linkCalls==beforeLinks+expectedCompiles &&
                    binaryCalls==beforeBinaries+programs-expectedCompiles,
                    std::string(label)+": edit escaped its dependencies (compiled "+
                    std::to_string(compileCalls-beforeCompiles)+", binaries "+std::to_string(binaryCalls-beforeBinaries)+")");
        };
        // Comments also change the exact source hash. Each dimension must
        // invalidate its own users, never unrelated biome or meshing sources.
        write(noise2Path,originalNoise2+"\n// 2D-only dependency edit\n");
        isolatedReload("2D noise",6.25f,12,27); // classification + 26 reliefs.
        write(volumePath,constantDelta(originalVolume,"0.1"));
        isolatedReload("3D evaluator with zero biome band",6.25f,13);
        // Only the 2D biome owns thickness: enabling it must reach the 3D pass
        // without recompiling that pass or changing any global field setting.
        write(reliefPath,planeRelief(originalRelief,"6.25","256.0"));
        isolatedReload("2D biome enables volume band",6.35f,16);
        write(volumePath,constantDelta(originalVolume,"0.5"));
        isolatedReload("Polar 3D function",6.75f,17);
        write(volumePath,constantDelta(originalVolume,"0.75"));
        isolatedReload("Polar 3D contribution",7.0f,15);
        write(noise3Path,originalNoise3+"\n// 3D-only dependency edit\n");
        isolatedReload("3D noise",7.0f,14,26);
        write(reliefPath,planeRelief(originalRelief,"6.25"));
        require(mesher.initialize(config,error),error);
        finishPlane(mesher,6.25f,18);

        const auto invalidLine = std::count(originalVolume.begin(), originalVolume.end(), '\n') + 2;
        write(volumePath, originalVolume + "\nPOLAR_RELOAD_SYNTAX_ERROR\n");
        require(!mesher.initialize(config, error), "Invalid merged biome GLSL unexpectedly compiled");
        require(error.find("biomes/PolarIce/volume.C.glsl") != std::string::npos &&
            error.find("GLSL source files") != std::string::npos &&
            error.find(std::to_string(invalidLine)) != std::string::npos,
            "Compiler error did not identify the merged source and original line: " + error);
        require(mesher.stats().allocatedBytes == 0 && !mesher.valid(second),
            "Failed recompilation retained stale allocations");
        write(volumePath, originalVolume);
        require(mesher.initialize(config, error), error);
        finishPlane(mesher, 6.25f, 3);

        std::filesystem::rename(volumePath, directory / "polar_volume.saved");
        require(!mesher.initialize(config, error) && error.find("biomes/PolarIce/volume.C.glsl") != std::string::npos,
            "Missing biome entry did not report its path");
        std::filesystem::rename(directory / "polar_volume.saved", volumePath);
        require(mesher.initialize(config, error), error);
        finishPlane(mesher, 6.25f, 4);
        mesher.shutdown();
        require(glGetError() == GL_NO_ERROR, "OpenGL error after failed/recovered reload");
        std::cout << "PASS shader reload: merged biome entry edit changes real vertices without process restart\n";
        if(binariesSupported)
            std::cout << "PASS terrain binary cache: 60 fixed programs; unchanged/settings-only regeneration loads all binaries; biome-local 2D/3D edits rebuild one program; shared noise edits rebuild only their dimension\n";
        std::cout << "PASS shader diagnostics: original merged-entry path/line, missing file and corrected-file retry\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL shader reload: " << error.what() << '\n';
        return 1;
    }
}
