#ifndef _RIGIDBODY_HPP
#define _RIGIDBODY_HPP

#include "glm/glm.hpp"
#include "btBulletDynamicsCommon.h"
#include "CollisionShape.hpp"
#include "Debug.hpp"
#include "GObject.hpp"
#include "Muscle.hpp"
#include "imgui-cmake/Header/imgui.h"

#include <memory>
#include <mutex>

namespace Ge
{
class RigidBody : public GObject
{
public:
    RigidBody(btDynamicsWorld* world);
    RigidBody(btDynamicsWorld* world, std::shared_ptr<CollisionShape> shape, bool hasInertia = true);
    void Translate(glm::vec3 const& translate);
    void setPosition(glm::vec3 pos) override;
    void setRotation(glm::quat rot) override;
    void setEulerAngles(glm::vec3 eul) override;
    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    glm::vec3 getEulerAngles() override;
    glm::vec3 getGravity() const;
    void SetMass(float mass);
    void setGravity(glm::vec3 const& gravity);
    void SetRestitution(float coef);
    void SetLinearVelocity(const glm::vec3 velocity);
    void SetAngularVelocity(const glm::vec3 velocity);
	void ResetDynamics(const glm::vec3 position, const glm::quat rotation);
    void SetAngularFactor(const glm::vec3 factor);
    void SetLinearFactor(const glm::vec3 factor);
    void SetDampling(const glm::vec2 dampling);
    void BuildPhysics(std::shared_ptr<CollisionShape> shape,bool hasInertia = true);
    bool IsInitialized();
    glm::vec3 GetLinearVelocity();
    glm::vec3 GetAngularVelocity();
    bool GetPhysicsSnapshot(
        glm::vec3& position,
        glm::quat& rotation,
        glm::vec3& linearVelocity,
        glm::vec3& angularVelocity,
        bool* active = nullptr) const;
    void capturePhysicsSnapshot();
    void ApplyImpulse(glm::vec3 impulse, glm::vec3 real_pos);
    void setCollisionFlags(int flags);
    void addCollisionFlags(int flags);
    int getCollisionFlags();
    void applyCentralImpulse(glm::vec3 impulse);
    void forceActivationState(int newState);
    void SetSleepingThreshold(float linear, float angular);
	void SetCcd(float motionThreshold, float sweptSphereRadius);
    void lockRotation();
    void activate(bool state);
    void setFriction(float f);
    const std::shared_ptr<CollisionShape> getCollisionShape() const;
    void onGUI() override;
    void mapMemory() override;
    virtual ~RigidBody();
    inline btRigidBody* GetBody(void)
    {
        return m_pBody;
    }
protected:
friend class PhysicsEngine;

    void publishPhysicsSnapshot();
    void setInWorldSnapshot(bool inWorld);

    btDynamicsWorld         * m_pWorld          = nullptr;
    btRigidBody             * m_pBody           = nullptr;
    btDefaultMotionState    * m_pMotionState    = nullptr;
    std::shared_ptr<CollisionShape> m_pShape;
    float m_gravity[3];
    float m_velocity[3];
    int style_idx = -1;
    bool m_activeCollision = false;
    std::vector<btGeneric6DofConstraint*> m_contraints;

    mutable std::mutex m_snapshotMutex;
    glm::vec3 m_snapshotPosition{ 0.0f };
    glm::quat m_snapshotRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 m_snapshotLinearVelocity{ 0.0f };
    glm::vec3 m_snapshotAngularVelocity{ 0.0f };
    glm::vec3 m_snapshotGravity{ 0.0f };
    bool m_snapshotInWorld = false;
    bool m_snapshotActive = true;
};



}

#endif
