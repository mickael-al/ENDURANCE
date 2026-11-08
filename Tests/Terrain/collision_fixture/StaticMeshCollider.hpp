#pragma once
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <memory>
namespace Ge
{
    class StaticMeshCollider
    {
    public:
        btBvhTriangleMeshShape* shape;
        std::shared_ptr<void> owner;
    };
}
