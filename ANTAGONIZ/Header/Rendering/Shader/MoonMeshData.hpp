#pragma once

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include <cstdint>
#include <vector>

namespace Ge
{
// Render-thread upload data. Positions retain the baked body's unit radius;
// detached fragments must not change its scale. Colors are linear, atlas sRGB.
struct MoonMeshData
{
    struct Vertex
    {
        glm::vec3 position{};
        glm::vec3 normal{0.f,1.f,0.f};
        glm::vec2 uv{};
        glm::vec4 color{1.f};
    };
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    unsigned width=0,height=0;
    std::vector<std::uint8_t> albedoRgba;
    float meanAlbedo=0.20f;
};
}
