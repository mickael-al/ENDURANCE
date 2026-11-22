cmake_minimum_required(VERSION 3.16)
project(EnduranceMoonBake LANGUAGES CXX)

# Optional native command-line entry point; the editor uses the same generator.
# Standalone: no OpenGL, engine, Python or external dependencies required.
add_executable(endurance_moon_bake
    bake_moon.cpp
    ../ANTAGONIZ/Source/ProjectUser/ProceduralMoon.cpp)
target_include_directories(endurance_moon_bake PRIVATE ../ANTAGONIZ/Header/ProjectUser)
target_compile_features(endurance_moon_bake PRIVATE cxx_std_17)
set_target_properties(endurance_moon_bake PROPERTIES CXX_EXTENSIONS OFF)
