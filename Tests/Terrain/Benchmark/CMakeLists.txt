cmake_minimum_required(VERSION 3.16)
project(EnduranceTerrainBenchmark LANGUAGES C CXX)
get_filename_component(_current_engine "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ENDURANCE_SOURCE_ROOT "${_current_engine}" CACHE PATH "Engine checkout to benchmark (can be the baseline branch)")
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# Match the /MD runtime used by this executable and bundled GLEW on Windows.
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
add_subdirectory("${ENDURANCE_SOURCE_ROOT}/ThirdParty/glew" "${CMAKE_CURRENT_BINARY_DIR}/glew")
add_subdirectory("${ENDURANCE_SOURCE_ROOT}/ThirdParty/glfw" "${CMAKE_CURRENT_BINARY_DIR}/glfw")
set(_shader "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Source/Rendering/Shader")
add_executable(terrain_generation_benchmark
    "${CMAKE_CURRENT_LIST_DIR}/../benchmark_generation.cpp"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp"
    "${_shader}/ShaderCompilation.cpp" "${_shader}/ShaderBinaryCache.cpp"
    "${_shader}/ShaderProgramBatch.cpp" "${_shader}/ParallelShaderCompiler.cpp" "${_shader}/ComputeShader.cpp")
target_compile_features(terrain_generation_benchmark PRIVATE cxx_std_17)
target_include_directories(terrain_generation_benchmark PRIVATE
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Struct"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Miscellaneous"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Rendering/Shader"
    "${ENDURANCE_SOURCE_ROOT}/ThirdParty")
target_link_libraries(terrain_generation_benchmark PRIVATE GLEW::GLEW glfw OpenGL::GL Threads::Threads)
target_compile_definitions(terrain_generation_benchmark PRIVATE GLFW_INCLUDE_NONE
    "TERRAIN_DEFAULT_SHADERS=\"${ENDURANCE_SOURCE_ROOT}/Asset/Shader/ProceduralTerrain\"")
file(READ "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainGpu.hpp" _gpu_header)
if(_gpu_header MATCHES "columnCacheEntries")
    target_compile_definitions(terrain_generation_benchmark PRIVATE TERRAIN_BENCHMARK_CACHE_API)
endif()
