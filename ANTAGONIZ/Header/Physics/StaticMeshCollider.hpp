#ifndef __STATIC_MESH_COLLIDER__
#define __STATIC_MESH_COLLIDER__

#include <btBulletDynamicsCommon.h>
#include <memory>

namespace Ge
{
    class StaticMeshCollider
    {
    public:
        StaticMeshCollider(btDynamicsWorld* world, btBvhTriangleMeshShape* meshShape);
        StaticMeshCollider(
            btDynamicsWorld* world,
            btBvhTriangleMeshShape* meshShape,
            const btTransform& worldTransform,
            const btVector3& scale,
            std::shared_ptr<void> resourceOwner = nullptr);
        ~StaticMeshCollider();
        void setFriction(float friction);
        void setRestitution(float restitution);
    private:
        btDynamicsWorld* m_world;
        btBvhTriangleMeshShape* m_shape;
        btCollisionShape* m_instanceShape;
        btRigidBody* m_body;
        btMotionState* m_motionState;
        std::shared_ptr<void> m_resourceOwner;
    };
}

#endif //! __STATIC_MESH_COLLIDER__
