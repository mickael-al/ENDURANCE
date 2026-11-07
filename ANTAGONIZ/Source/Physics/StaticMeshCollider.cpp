#include "StaticMeshCollider.hpp"

#include <utility>

namespace Ge
{
    StaticMeshCollider::StaticMeshCollider(
        btDynamicsWorld* world,
        btBvhTriangleMeshShape* meshShape)
        : StaticMeshCollider(
            world,
            meshShape,
            btTransform::getIdentity(),
            btVector3(1.0f, 1.0f, 1.0f),
            nullptr)
    {
    }

    StaticMeshCollider::StaticMeshCollider(
        btDynamicsWorld* world,
        btBvhTriangleMeshShape* meshShape,
        const btTransform& worldTransform,
        const btVector3& scale,
        std::shared_ptr<void> resourceOwner)
        : m_world(world),
        m_shape(meshShape),
        m_instanceShape(nullptr),
        m_body(nullptr),
        m_motionState(nullptr),
        m_resourceOwner(std::move(resourceOwner))
    {
        if (!m_world || !m_shape)
        {
            return;
        }

        const btVector3 safeScale(
            btMax(btFabs(scale.x()), btScalar(0.001f)),
            btMax(btFabs(scale.y()), btScalar(0.001f)),
            btMax(btFabs(scale.z()), btScalar(0.001f)));
        btCollisionShape* bodyShape = m_shape;
        if ((safeScale - btVector3(1.0f, 1.0f, 1.0f)).length2() >
            SIMD_EPSILON * SIMD_EPSILON)
        {
            m_instanceShape = new btScaledBvhTriangleMeshShape(
                m_shape,
                safeScale);
            bodyShape = m_instanceShape;
        }

        btScalar mass(0.0f);
        btVector3 inertia(0, 0, 0);
        m_motionState = new btDefaultMotionState(worldTransform);
        btRigidBody::btRigidBodyConstructionInfo rbInfo(
            mass,
            m_motionState,
            bodyShape,
            inertia);
        m_body = new btRigidBody(rbInfo);
        m_body->setCollisionFlags(m_body->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);

        m_world->addRigidBody(m_body);
    }

    StaticMeshCollider::~StaticMeshCollider()
    {
        if (m_body && m_world)
        {
            m_world->removeRigidBody(m_body);
        }

        delete m_body;
        delete m_motionState;
        delete m_instanceShape;
    }

    void StaticMeshCollider::setFriction(float friction)
    {
        if (m_body != nullptr)
        {
            m_body->setFriction(friction);
        }
    }

    void StaticMeshCollider::setRestitution(float restitution)
    {
        if (m_body != nullptr)
        {
            m_body->setRestitution(restitution);
        }
    }

}
