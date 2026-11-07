#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "ShapeBufferBase.hpp"
// Minimal imported-rig facade. These are the fields accessed by production
// cloth; FBX import/animation are tested separately with the actual protagonist.
struct Bone {
    std::uint16_t idGlobal=0;
    std::int16_t parent=-1;
    std::string name;
    glm::mat4 bindPose{1.0f},inverseBindPose{1.0f};
};
struct Skeleton {
    std::vector<Bone> bones;
    glm::mat4 bindTransform{1.0f},inverseBindTransform{1.0f};
};
namespace Ge {
class ShapeBufferSkeleton {
public:
    ShapeBufferSkeleton(ShapeBufferBase* shape) : m_shapes{shape} {
        Bone bone;bone.name="Root";m_skeleton.bones.push_back(bone);
    }
    const Skeleton& getSkeleton() const { ClothTest::mainOnly();return m_skeleton; }
    const std::vector<ShapeBufferBase*>& getShapes() const { ClothTest::mainOnly();return m_shapes; }
private:
    Skeleton m_skeleton;
    std::vector<ShapeBufferBase*> m_shapes;
};
}
