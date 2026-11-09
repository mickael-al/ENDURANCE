#pragma once
// Test adapter for the two physics-queue operations used by CollisionCache.
// The production cache, OpenGL transfers and Bullet BVH/mesh implementation are
// linked unchanged. A controllable publication gate exercises worker lifetime.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
class btBvhTriangleMeshShape;
namespace Ge
{
    class StaticMeshCollider;
    class PhysicsWraper
    {
    public:
        StaticMeshCollider* AllocateStaticMeshCollider(btBvhTriangleMeshShape*,glm::vec3,glm::quat,
                                                     glm::vec3,std::shared_ptr<void>);
        void ReleaseStaticMeshCollider(StaticMeshCollider*);
        void block(bool value);
        std::mutex mutex;
        std::condition_variable wake;
        bool blocked=false, invalid=false;
        std::set<std::thread::id> builders;
        std::set<StaticMeshCollider*> live;
        std::vector<std::array<float,9>> triangles;
        unsigned releases=0, entered=0;
    };
}
