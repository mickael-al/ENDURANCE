#ifndef _COLLISIONBODY_HPP
#define _COLLISIONBODY_HPP

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "btBulletDynamicsCommon.h"
#include "CollisionShape.hpp"
#include "Debug.hpp"
#include "GObject.hpp"

#include <memory>

namespace Ge
{
    class CollisionBody final : public GObject
    {
    public:
        CollisionBody(btDynamicsWorld* world);
        CollisionBody(btDynamicsWorld* world, std::shared_ptr<CollisionShape> shape);
        void Build(std::shared_ptr<CollisionShape> shape);
        void setPosition(glm::vec3 pos) override;
        void setRotation(glm::quat rot) override;
		void setTransform(glm::vec3 pos, glm::quat rot);
        void setEulerAngles(glm::vec3 eul) override;
        void setScale(glm::vec3 scale) override;
        glm::vec3 getPosition() const override;
        glm::quat getRotation() const override;
        glm::vec3 getEulerAngles() override;
        glm::vec3 getScale() const override;
        void setFriction(float friction);
        void setRestitution(float restitution);
        bool IsInitialized();
        const std::shared_ptr<CollisionShape> getCollisionShape() const;
        void mapMemory() override;
        virtual ~CollisionBody();
    protected:
        friend class PhysicsEngine;

        inline btCollisionObject* GetBody(void) const
        {
            return m_pBody;
        }

        btDynamicsWorld* m_pWorld = nullptr;
        btCollisionObject* m_pBody = nullptr;        
        std::shared_ptr<CollisionShape> m_pShape;
    };



}

#endif