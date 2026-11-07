#pragma once
#include <glm/glm.hpp>
#include "ShapeBufferSkeleton.hpp"
namespace Ge {
class SkinnedTransform {
public:
    explicit SkinnedTransform(ShapeBufferSkeleton* buffer):pose(1,glm::mat4(1)),m_buffer(buffer) {}
    ShapeBufferSkeleton* getBuffer() const { ClothTest::mainOnly();return m_buffer; }
    const std::vector<glm::mat4>& getGlobalPose() const { ClothTest::mainOnly();return pose; }
    const glm::mat4& getModelMatrix() const { ClothTest::mainOnly();return world; }
    std::vector<glm::mat4> pose;
    glm::mat4 world{1.0f};
private:
    ShapeBufferSkeleton* m_buffer;
};
}
