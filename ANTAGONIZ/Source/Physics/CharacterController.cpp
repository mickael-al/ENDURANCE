#include "CharacterController.hpp"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"

#include <cmath>

namespace
{
    constexpr float FixedPhysicsStep = 1.0f / 60.0f;
    constexpr float CharacterPenetrationDepth = 0.03f;
    constexpr float CapsuleHeightEpsilon = 0.0001f;
    constexpr float CeilingNormalThreshold = 0.01f;

    class ClosestCeilingSweepResult final
        : public btCollisionWorld::ClosestConvexResultCallback
    {
    public:
        ClosestCeilingSweepResult(
            const btCollisionObject* character,
            const btVector3& up,
            const btVector3& from,
            const btVector3& to)
            : btCollisionWorld::ClosestConvexResultCallback(from, to),
              m_character(character),
              m_up(up)
        {
        }

        btScalar addSingleResult(
            btCollisionWorld::LocalConvexResult& result,
            bool normalInWorldSpace) override
        {
            if (result.m_hitCollisionObject == nullptr ||
                result.m_hitCollisionObject == m_character ||
                !result.m_hitCollisionObject->hasContactResponse())
            {
                return btScalar(1.0f);
            }

            const btVector3 normal = normalInWorldSpace
                ? result.m_hitNormalLocal
                : result.m_hitCollisionObject->getWorldTransform().getBasis() *
                    result.m_hitNormalLocal;
            if (normal.dot(m_up) >= -CeilingNormalThreshold)
            {
                return btScalar(1.0f);
            }

            return ClosestConvexResultCallback::addSingleResult(
                result,
                normalInWorldSpace);
        }

    private:
        const btCollisionObject* m_character = nullptr;
        btVector3 m_up{ 0.0f, 1.0f, 0.0f };
    };
}

namespace Ge
{
    CharacterController::CharacterController(btDynamicsWorld* world, float radius, float height, float stepHeight)
        : m_world(world),
          m_stepHeight(stepHeight),
          m_radius(radius),
          m_standingCylinderHeight(height),
          m_currentCylinderHeight(height)
    {

        // Shape (capsule)
        m_shape = new btCapsuleShape(radius, height);

        // Ghost object
        m_ghostObject = new btPairCachingGhostObject();
        m_ghostObject->setCollisionShape(m_shape);
        m_ghostObject->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

        // Position initiale
        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(0, 5, 0));
        m_ghostObject->setWorldTransform(startTransform);

        // Controller
        m_controller = new btKinematicCharacterController(
            m_ghostObject,
            m_shape,
            m_stepHeight,
            btVector3(0.0f, 1.0f, 0.0f)
        );

        // Bullet rotates its ghost from an initial Z-up frame. Our capsule
        // is already Y-up, including the head-clearance and crouch queries.
        m_ghostObject->setWorldTransform(startTransform);
        m_ghostObject->setInterpolationWorldTransform(startTransform);

        // Ajout au monde
        m_world->addCollisionObject(
            m_ghostObject,
            btBroadphaseProxy::CharacterFilter,
            btBroadphaseProxy::StaticFilter |
            btBroadphaseProxy::DefaultFilter |
            btBroadphaseProxy::CharacterFilter
        );
        
        m_world->addAction(m_controller);    
        m_actionStatus = true;
        // Paramètres utiles
        m_controller->setGravity(btVector3(0, -9.81f, 0));
        m_controller->setMaxSlope(btRadians(45.0f));
        m_controller->setMaxPenetrationDepth(CharacterPenetrationDepth);
        // A step/jump can reach a ceiling or thin wall outside the ghost's
        // current overlaps. Query the entire swept volume in the world.
        m_controller->setUseGhostSweepTest(false);
        publishPhysicsSnapshot();
    }

    void CharacterController::setPosition(glm::vec3 pos)
    {
        btTransform t = m_ghostObject->getWorldTransform();
        t.setOrigin(btVector3(pos.x, pos.y, pos.z));
        m_ghostObject->setWorldTransform(t);
        publishPhysicsSnapshot();
    }

    glm::vec3 CharacterController::getPosition() const
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshotPosition;
    }

    void CharacterController::setLinearVelocity(glm::vec3 vel)
    {
        m_controller->setLinearVelocity(btVector3(vel.x, vel.y, vel.z));
    }

    void CharacterController::applyImpulse(glm::vec3 vel)
    {
        m_controller->applyImpulse(btVector3(vel.x, vel.y, vel.z));
    }

    glm::vec3 CharacterController::getLinearVelocity()
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshotLinearVelocity;
    }

    void CharacterController::setWalkDirection(glm::vec3 dir)
    {
        m_controller->setWalkDirection(btVector3(dir.x, dir.y, dir.z));
    }

    void CharacterController::setMovementVelocity(glm::vec3 velocity)
    {
        // btKinematicCharacterController expects a displacement per physics
        // substep. Keep it active until the next command so every catch-up
        // substep receives the same world-space velocity.
        m_controller->setWalkDirection(btVector3(
            velocity.x * FixedPhysicsStep,
            velocity.y * FixedPhysicsStep,
            velocity.z * FixedPhysicsStep));
    }

    void CharacterController::setCapsuleCylinderHeight(float height)
    {
        if (m_shape == nullptr || m_ghostObject == nullptr ||
            m_controller == nullptr || m_world == nullptr)
        {
            return;
        }

        const float targetHeight = glm::clamp(
            height,
            0.0f,
            m_standingCylinderHeight);
        if (std::abs(targetHeight - m_currentCylinderHeight) <=
            CapsuleHeightEpsilon)
        {
            return;
        }
        if (targetHeight > m_currentCylinderHeight &&
            !hasCapsuleHeightClearance(targetHeight))
        {
            return;
        }

        btCapsuleShape* capsule = static_cast<btCapsuleShape*>(m_shape);
        const btVector3 up(0.0f, 1.0f, 0.0f);
        const bool wasGrounded = m_controller->onGround();
        const btVector3 retainedMovement =
            m_controller->getLinearVelocity() -
            up * m_controller->getLinearVelocity().dot(up);

        // Anchor the resize to the actual lower end of the Bullet capsule.
        // Deriving both half-heights from the shape is safer than assuming
        // that only the cylinder contributes to the scaled support point.
        btTransform transform = m_ghostObject->getWorldTransform();
        const float previousHalfHeight =
            capsule->getHalfHeight() + capsule->getRadius();
        const btVector3 feetPosition =
            transform.getOrigin() - up * previousHalfHeight;
        const float scaleY = m_standingCylinderHeight > CapsuleHeightEpsilon
            ? targetHeight / m_standingCylinderHeight
            : 1.0f;
        capsule->setLocalScaling(btVector3(1.0f, scaleY, 1.0f));

        const float targetHalfHeight =
            capsule->getHalfHeight() + capsule->getRadius();
        transform.setOrigin(feetPosition + up * targetHalfHeight);
        m_ghostObject->setWorldTransform(transform);
        m_ghostObject->setInterpolationWorldTransform(transform);
        m_currentCylinderHeight = targetHeight;

        if (m_ghostObject->getBroadphaseHandle() != nullptr)
        {
            m_world->getBroadphase()->getOverlappingPairCache()
                ->cleanProxyFromPairs(
                    m_ghostObject->getBroadphaseHandle(),
                    m_world->getDispatcher());
            m_world->updateSingleAabb(m_ghostObject);
        }

        // Resizing invalidates Bullet's cached contact pairs. When the player
        // was grounded, keep the controller's vertical state grounded while
        // the refreshed pairs are picked up on the next physics step. The
        // horizontal movement command is preserved verbatim.
        if (wasGrounded)
        {
            m_controller->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
            // setWalkDirection expects a displacement per Bullet step, while
            // getLinearVelocity returns units per second.
            m_controller->setWalkDirection(retainedMovement * FixedPhysicsStep);
        }

        // Consumers render and replicate the capsule centre. Publish the new
        // centre immediately so crouch height and position always describe
        // the same pose, including while the player is perfectly still.
        publishPhysicsSnapshot();
    }

    bool CharacterController::canStand() const
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshotCanStand;
    }

    bool CharacterController::hasCapsuleHeightClearance(
        float targetCylinderHeight) const
    {
        if (m_world == nullptr || m_ghostObject == nullptr ||
            targetCylinderHeight <=
                m_currentCylinderHeight + CapsuleHeightEpsilon)
        {
            return true;
        }

        // Keeping the feet fixed means that only the upper cap moves. Sweeping
        // one sphere from the current upper-cap centre to its target centre
        // covers exactly the volume that a taller capsule would add.
        btSphereShape headSphere(m_radius);
        btTransform start;
        start.setIdentity();
        start.setOrigin(
            m_ghostObject->getWorldTransform().getOrigin() +
            btVector3(0.0f, m_currentCylinderHeight * 0.5f, 0.0f));
        btTransform end = start;
        end.getOrigin() += btVector3(
            0.0f,
            targetCylinderHeight - m_currentCylinderHeight,
            0.0f);

        ClosestCeilingSweepResult callback(
            m_ghostObject,
            btVector3(0.0f, 1.0f, 0.0f),
            start.getOrigin(),
            end.getOrigin());
        if (m_ghostObject->getBroadphaseHandle() != nullptr)
        {
            callback.m_collisionFilterGroup =
                m_ghostObject->getBroadphaseHandle()->m_collisionFilterGroup;
            callback.m_collisionFilterMask =
                m_ghostObject->getBroadphaseHandle()->m_collisionFilterMask;
        }
        m_world->convexSweepTest(
            &headSphere,
            start,
            end,
            callback,
            btScalar(0.0f));
        return !callback.hasHit();
    }

    void CharacterController::jump()
    {
        if (m_controller->canJump())
        {
            m_controller->jump();                   
        }
    }

    bool CharacterController::onGround() const
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshotOnGround;
    }

    void CharacterController::setGravity(glm::vec3 gravity)
    {
        m_controller->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
    }

    void CharacterController::setMaxSlope(float degres)
    {
        m_controller->setMaxSlope(glm::radians(degres));
    }

    void CharacterController::setWarp(glm::vec3 pos)
    {
        m_controller->reset(m_world);
        m_controller->setLinearVelocity(btVector3(0, 0, 0));
        m_controller->warp(btVector3(pos.x, pos.y, pos.z));
        publishPhysicsSnapshot();
    }

    void CharacterController::setFallSpeed(float speed)
    {
        m_controller->setFallSpeed(speed);
    }

    void CharacterController::setJumpSpeed(float speed)
    {
        m_controller->setJumpSpeed(speed);
    }

    void CharacterController::setMaxJumpHeight(float height)
    {
        m_controller->setMaxJumpHeight(height);        
    }

    void CharacterController::setUpInterpolate(bool state)
    {
        m_controller->setUpInterpolate(state);        
    }

    void CharacterController::setActionStatus(bool state)
    {
        if (state)
        {
            if (m_actionStatus != state)
            {
                m_world->addAction(m_controller);
                m_actionStatus = true;
            }
        }
        else
        {
            if (m_actionStatus != state)
            {
                m_world->removeAction(m_controller);
                m_actionStatus = false;
            }
        }
    }

    void CharacterController::setStepHeight(float height)
    {
        m_controller->setStepHeight(height);
    }

    bool CharacterController::canJump()
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshotCanJump;
    }

    void CharacterController::capturePhysicsSnapshot()
    {
        publishPhysicsSnapshot();
    }

    void CharacterController::publishPhysicsSnapshot()
    {
        const btVector3 position = m_ghostObject->getWorldTransform().getOrigin();
        const btVector3 velocity = m_controller->getLinearVelocity();
        const bool canStand =
            hasCapsuleHeightClearance(m_standingCylinderHeight);

        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshotPosition = glm::vec3(position.x(), position.y(), position.z());
        m_snapshotLinearVelocity = glm::vec3(velocity.x(), velocity.y(), velocity.z());
        m_snapshotOnGround = m_controller->onGround();
        m_snapshotCanJump = m_controller->canJump();
        m_snapshotCanStand = canStand;
    }


    CharacterController::~CharacterController()
    {
        if (m_world)
        {
            if (m_actionStatus)
            {
                m_world->removeAction(m_controller);
                m_actionStatus = false;
            }
            m_world->removeCollisionObject(m_ghostObject);
        }

        delete m_controller;
        delete m_ghostObject;
        delete m_shape;
    }

}
