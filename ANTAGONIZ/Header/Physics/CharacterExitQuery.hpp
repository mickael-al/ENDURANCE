#ifndef PARASITE_CHARACTER_EXIT_QUERY_HPP
#define PARASITE_CHARACTER_EXIT_QUERY_HPP

#include "CharacterExitPlacement.hpp"
#include "BulletCollision/CollisionDispatch/btCollisionWorld.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"

namespace Ge
{
// Run only on the physics thread. All probes in one search see the same world.
class CharacterExitQuery
{
public:
    CharacterExitQuery(btCollisionWorld& world, const CharacterExitRequest& request)
        : m_world(world), m_shape(request.radius + 0.02f,
            (std::max)(0.0f, 2.0f * (request.halfHeight - request.radius))) {}

    bool groundRay(glm::vec3 from, glm::vec3 to, glm::vec3& point, glm::vec3& normal)
    {
        GroundRay callback(toBt(from), toBt(to));
        m_world.rayTest(toBt(from), toBt(to), callback);
        if (!callback.hasHit()) return false;
        point = toGlm(callback.m_hitPointWorld);
        normal = toGlm(callback.m_hitNormalWorld);
        return true;
    }

    bool clearCapsule(glm::vec3 centre)
    {
        btCollisionObject probe;
        probe.setCollisionShape(&m_shape);
        probe.setWorldTransform(transform(centre));
        Contacts callback;
        m_world.contactTest(&probe, callback);
        return !callback.blocked;
    }

    bool clearPath(glm::vec3 from, glm::vec3 to)
    {
        PathSweep callback(toBt(from), toBt(to));
        m_world.convexSweepTest(&m_shape, transform(from), transform(to), callback);
        return !callback.hasHit();
    }

private:
    static constexpr int Mask = btBroadphaseProxy::StaticFilter |
        btBroadphaseProxy::DefaultFilter | btBroadphaseProxy::CharacterFilter;
    static bool solid(const btBroadphaseProxy* proxy, bool includeCharacters)
    {
        const auto* object = static_cast<const btCollisionObject*>(proxy->m_clientObject);
        return object != nullptr && object->hasContactResponse() &&
            (includeCharacters || !(object->getCollisionFlags() & btCollisionObject::CF_CHARACTER_OBJECT));
    }
    struct GroundRay : btCollisionWorld::ClosestRayResultCallback
    {
        GroundRay(btVector3 from, btVector3 to) : ClosestRayResultCallback(from, to)
        { m_collisionFilterGroup = btBroadphaseProxy::CharacterFilter; m_collisionFilterMask = Mask; }
        bool needsCollision(btBroadphaseProxy* proxy) const override
        { return ClosestRayResultCallback::needsCollision(proxy) && solid(proxy, false); }
    };
    struct PathSweep : btCollisionWorld::ClosestConvexResultCallback
    {
        PathSweep(btVector3 from, btVector3 to) : ClosestConvexResultCallback(from, to)
        { m_collisionFilterGroup = btBroadphaseProxy::CharacterFilter; m_collisionFilterMask = Mask; }
        bool needsCollision(btBroadphaseProxy* proxy) const override
        { return ClosestConvexResultCallback::needsCollision(proxy) && solid(proxy, false); }
    };
    struct Contacts : btCollisionWorld::ContactResultCallback
    {
        bool blocked = false;
        Contacts()
        { m_collisionFilterGroup = btBroadphaseProxy::CharacterFilter; m_collisionFilterMask = Mask; }
        bool needsCollision(btBroadphaseProxy* proxy) const override
        { return ContactResultCallback::needsCollision(proxy) && solid(proxy, true); }
        btScalar addSingleResult(btManifoldPoint& contact, const btCollisionObjectWrapper*,
            int, int, const btCollisionObjectWrapper*, int, int) override
        {
            if (contact.getDistance() < btScalar(0.0)) blocked = true;
            return btScalar(0.0);
        }
    };
    static btVector3 toBt(glm::vec3 v) { return btVector3(v.x, v.y, v.z); }
    static glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }
    static btTransform transform(glm::vec3 position)
    {
        btTransform t;
        t.setIdentity(); t.setOrigin(toBt(position));
        return t;
    }
    btCollisionWorld& m_world;
    btCapsuleShape m_shape;
};
}
#endif
