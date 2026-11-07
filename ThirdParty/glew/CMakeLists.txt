cmake_minimum_required(VERSION 3.10)
project(GLEW LANGUAGES C)

# Build the exact bundled GLEW sources on Windows and Linux.
# The source repository does not contain the Windows .lib expected by its old wrapper.
find_package(OpenGL REQUIRED)
add_library(antagoniz_glew STATIC src/glew.c)
add_library(GLEW::GLEW ALIAS antagoniz_glew)
target_include_directories(antagoniz_glew PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(antagoniz_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
target_link_libraries(antagoniz_glew PUBLIC OpenGL::GL)

