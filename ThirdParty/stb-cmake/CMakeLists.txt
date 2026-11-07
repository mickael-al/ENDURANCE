cmake_minimum_required(VERSION 3.10)
project(stb LANGUAGES C)

# Keep the original implementation and image-format definitions in stb_shared.c.
add_library(stb STATIC
    stb_shared.c
    stb_image.h
    stb_image_write.h
)
target_include_directories(stb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
