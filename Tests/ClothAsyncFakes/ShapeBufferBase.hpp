#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "Vertex.hpp"
#include "glcore.hpp"
namespace Ge {
class ShapeBufferBase {
public:
    ShapeBufferBase(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
        : m_vertices(std::move(vertices)),m_indices(std::move(indices)) {}
    const std::vector<Vertex>& getVertices() const { ClothTest::mainOnly();return m_vertices; }
    const std::vector<std::uint32_t>& getIndices() const { ClothTest::mainOnly();return m_indices; }
private:
    std::vector<Vertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
};
}
