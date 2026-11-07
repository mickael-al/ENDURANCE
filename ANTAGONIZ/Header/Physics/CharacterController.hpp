#ifndef __CHARACTER_CONTROLLER_HPP__
#define __CHARACTER_CONTROLLER_HPP__

#include "glm/glm.hpp"
#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/Character/btKinematicCharacterController.h"
#include "BulletCollision/CollisionDispatch/btGhostObject.h"
#include <mutex>
namespace Ge
{
    class CharacterController
    {
    public:
        CharacterController(btDynamicsWorld* world, float radius, float height, float stepHeight = 0.3f);

        void setPosition(glm::vec3 pos);
        void setWarp(glm::vec3 pos);
        glm::vec3 getPosition() const;

        void setWalkDirection(glm::vec3 dir);
        void setMovementVelocity(glm::vec3 velocity);
        void setCapsuleCylinderHeight(float height);
        bool canStand() const;
        void jump();
        bool onGround() const;

        void setGravity(glm::vec3 gravity);
        void setMaxSlope(float degres);        
        void setFallSpeed(float speed);
        void setJumpSpeed(float speed);
        void setMaxJumpHeight(float height);
        void setUpInterpolate(bool state);
        void setActionStatus(bool state);
        void setStepHeight(float height);
        void applyImpulse(glm::vec3 vel);
        void setLinearVelocity(glm::vec3 vel);
        glm::vec3 getLinearVelocity();
        bool canJump();
        void capturePhysicsSnapshot();

        virtual ~CharacterController();

    private:
        bool hasCapsuleHeightClearance(float targetCylinderHeight) const;
        void publishPhysicsSnapshot();

        btDynamicsWorld* m_world = nullptr;

        btPairCachingGhostObject* m_ghostObject = nullptr;
        btConvexShape* m_shape = nullptr;
        btKinematicCharacterController* m_controller = nullptr;
        bool m_actionStatus = false;
        float m_stepHeight = 0.3f;
        float m_radius = 0.0f;
        float m_standingCylinderHeight = 0.0f;
        float m_currentCylinderHeight = 0.0f;

        mutable std::mutex m_snapshotMutex;
        glm::vec3 m_snapshotPosition{ 0.0f };
        glm::vec3 m_snapshotLinearVelocity{ 0.0f };
        bool m_snapshotOnGround = false;
        bool m_snapshotCanJump = false;
        bool m_snapshotCanStand = true;
    };

}

#endif//!__CHARACTER_CONTROLLER_HPP__