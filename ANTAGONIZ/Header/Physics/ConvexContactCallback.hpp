#ifndef __CONVEX_CONTACT_CALLBACK__
#define __CONVEX_CONTACT_CALLBACK__

#include <glm/glm.hpp>
#include "btBulletDynamicsCommon.h"
#include <vector>
#include <unordered_set>

namespace Ge
{
    struct ConvexHit
    {
        btCollisionObject* object;
        glm::vec3 point;
        glm::vec3 normal;
    };

    class ConvexContactCallback : public btCollisionWorld::ContactResultCallback
    {
    public:
        const btCollisionObject* testObject;

        std::vector<ConvexHit>* hits;
        const std::unordered_set<const btCollisionObject*>& ignore;

        int m_filterGroup;
        int m_filterMask;

        ConvexContactCallback(
            const btCollisionObject* obj,
            std::vector<ConvexHit>* h,
            const std::unordered_set<const btCollisionObject*>& i)
            : testObject(obj), hits(h), ignore(i)
        {
            m_filterGroup = btBroadphaseProxy::DefaultFilter;
            m_filterMask =
                btBroadphaseProxy::StaticFilter |
                btBroadphaseProxy::DefaultFilter;
        }

        bool needsCollision(btBroadphaseProxy* proxy0) const override
        {
            if (!btCollisionWorld::ContactResultCallback::needsCollision(proxy0))
            {
                return false;
            }
            if (!proxy0 || !proxy0->m_clientObject)
            {
                return false;
            }

            const btCollisionObject* other = static_cast<const btCollisionObject*>(proxy0->m_clientObject);
            
            if (!other)
            {
                return false;
            }
            
            if (ignore.count(other))
            {
                return false;
            }
            
            if (!(proxy0->m_collisionFilterGroup & m_filterMask))
            {
                return false;
            }

            return true;
        }

        btScalar addSingleResult(
            btManifoldPoint& cp,
            const btCollisionObjectWrapper* colObj0Wrap,int, int,
            const btCollisionObjectWrapper* colObj1Wrap,int, int
        ) override
        {
            if (!colObj0Wrap || !colObj1Wrap)
            {
                return 0;
            }

            const btCollisionObject* obj0 = colObj0Wrap->getCollisionObject();
            const btCollisionObject* obj1 = colObj1Wrap->getCollisionObject();

            if (!obj0 || !obj1)
            {
                return 0;
            }
            
            const btCollisionObject* other = nullptr;

            if (obj0 == testObject)
            {
                other = obj1;
            }
            else
            {
                other = obj0;
            }

            if (!other)
            {
                return 0;
            }

            ConvexHit hit;

            btVector3 p = cp.getPositionWorldOnB();
            btVector3 n = cp.m_normalWorldOnB;

            hit.object = const_cast<btCollisionObject*>(other);
            hit.point = glm::vec3(p.x(), p.y(), p.z());
            hit.normal = glm::vec3(n.x(), n.y(), n.z());

            hits->push_back(hit);

            return 0;
        }
    };
}

#endif // __CONVEX_CONTACT_CALLBACK__