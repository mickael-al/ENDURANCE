#ifndef __RIGID_WRAPER__
#define __RIGID_WRAPER__

#include <glm/glm.hpp>
#include <memory>

namespace Ge
{
	class RigidBody;
    class CommandQueue;
    class CollisionShape;
	class RigidWraper
	{
	public:
		RigidWraper(RigidBody * rb, CommandQueue* queue);
        void Translate(glm::vec3 const& translate);
        void setPosition(glm::vec3 pos);
        void setRotation(glm::quat rot);
        void setEulerAngles(glm::vec3 eul);
        glm::vec3 getPosition() const;
        glm::quat getRotation() const;
        glm::vec3 getEulerAngles();
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
        void BuildPhysics(std::shared_ptr<CollisionShape> shape, bool hasInertia = true);
        bool IsInitialized();
        glm::vec3 GetLinearVelocity();
        glm::vec3 GetAngularVelocity();
        bool GetPhysicsSnapshot(
            glm::vec3& position,
            glm::quat& rotation,
            glm::vec3& linearVelocity,
            glm::vec3& angularVelocity,
            bool* active = nullptr) const;
        void ApplyImpulse(glm::vec3 impulse, glm::vec3 real_pos);
        void setCollisionFlags(int flags);
        void addCollisionFlags(int flags);
        int getCollisionFlags();
        void applyCentralImpulse(glm::vec3 impulse);
        void forceActivationState(int newState);
        void SetSleepingThreshold(float linear, float angular);
		void SetCcd(float motionThreshold, float sweptSphereRadius);
        void activate(bool state);
        void lockRotation();
        void setFriction(float f);
        bool wrapsRigidBody(const RigidBody* body) const { return m_rb == body; }
        void onGUI();
    private:
        friend class PhysicsWraper;
        friend class SliderWraper;
        RigidBody* getRigidBody() const;
	private:
		RigidBody* m_rb;
        CommandQueue* m_queue;
	};
}

#endif //!__RIGID_WRAPER__
