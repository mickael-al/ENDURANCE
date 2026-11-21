cmake_minimum_required(VERSION 3.16)
project(EndurancePerformance LANGUAGES C CXX)
get_filename_component(_current_engine "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ENDURANCE_SOURCE_ROOT "${_current_engine}" CACHE PATH "Engine checkout to benchmark")
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
add_subdirectory("${ENDURANCE_SOURCE_ROOT}/ThirdParty/glew" "${CMAKE_CURRENT_BINARY_DIR}/glew")
add_subdirectory("${ENDURANCE_SOURCE_ROOT}/ThirdParty/glfw" "${CMAKE_CURRENT_BINARY_DIR}/glfw")
set(_shader "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Source/Rendering/Shader")
set(_project "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Source/ProjectUser")
add_executable(endurance_performance benchmark.cpp
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp"
    "${_project}/EnduranceObjectPlacement.cpp" "${_project}/TreeRenderGpu.cpp"
    "${_project}/ProceduralTree.cpp" "${_project}/ProceduralTreeBake.cpp"
    "${_shader}/ShaderCompilation.cpp" "${_shader}/ShaderBinaryCache.cpp"
    "${_shader}/ShaderProgramBatch.cpp" "${_shader}/ParallelShaderCompiler.cpp" "${_shader}/ComputeShader.cpp"
    "${ENDURANCE_SOURCE_ROOT}/ThirdParty/stb-cmake/stb_shared.c")
target_compile_features(endurance_performance PRIVATE cxx_std_17)
target_include_directories(endurance_performance PRIVATE
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Struct"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Miscellaneous"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Interface"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/ProjectUser"
    "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Rendering/Shader"
    "${ENDURANCE_SOURCE_ROOT}/ThirdParty" "${ENDURANCE_SOURCE_ROOT}/ThirdParty/glm")
target_link_libraries(endurance_performance PRIVATE GLEW::GLEW glfw OpenGL::GL Threads::Threads)
target_compile_definitions(endurance_performance PRIVATE GLFW_INCLUDE_NONE GLEW_NO_GLU NOMINMAX)
if(MSVC)
    target_compile_options(endurance_performance PRIVATE /O2 /fp:fast)
else()
    target_compile_options(endurance_performance PRIVATE -O3 -ffast-math)
endif()
file(READ "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainObjects.hpp" _object_header)
if(_object_header MATCHES "allocationRevision")
    target_compile_definitions(endurance_performance PRIVATE ENDURANCE_PERF_ALLOCATION_REVISION)
endif()
file(READ "${ENDURANCE_SOURCE_ROOT}/ANTAGONIZ/Header/ProjectUser/TreeRenderGpu.hpp" _tree_header)
if(_tree_header MATCHES "transformedInstances" AND _tree_header MATCHES "transformPagesBuilt" AND _tree_header MATCHES "transformPagesReused" AND _tree_header MATCHES "indirectCommands")
    target_compile_definitions(endurance_performance PRIVATE ENDURANCE_PERF_INCREMENTAL_TREES)
endif()
