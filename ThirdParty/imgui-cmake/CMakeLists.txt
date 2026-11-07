cmake_minimum_required(VERSION 3.10)
project(ImGUI LANGUAGES CXX)

# Preserve the bundled docking snapshot and the backends used by the engine.
add_library(ImGUI STATIC
    Source/imgui.cpp
    Source/imgui_draw.cpp
    Source/imgui_widgets.cpp
    Source/imgui_tables.cpp
    Source/imgui_stdlib.cpp
    Source/imgui_impl_glfw.cpp
    Source/imgui_impl_opengl3.cpp
)
target_include_directories(ImGUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Header")
# The GLFW backend does not need platform OpenGL headers; the OpenGL 3 backend uses its own loader.
target_compile_definitions(ImGUI PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(ImGUI PRIVATE glfw ${CMAKE_DL_LIBS})
