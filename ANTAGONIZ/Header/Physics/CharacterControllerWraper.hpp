#ifndef __CHARACTER_CONTROLLER_WRAPER__
#define __CHARACTER_CONTROLLER_WRAPER__

#include <glm/glm.hpp>
#include <memory>

namespace Ge
{
	class CharacterController;
	class CommandQueue;
	class CharacterControllerWraper
	{
	public:
		CharacterControllerWraper(CharacterController * cc, CommandQueue* queue);
		void setPosition(glm::vec3 pos);
		glm::vec3 getPosition() const;
		void setLinearVelocity(glm::vec3 vel);
		void applyImpulse(glm::vec3 vel);
		glm::vec3 getLinearVelocity() const;
		void setWarp(glm::vec3 pos);
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
		bool canJump();

	private:
		friend class PhysicsWraper;
		CharacterController* getCharacterController() const;
	private:
		CharacterController* m_cc;
		CommandQueue* m_queue;
	};
}
#endif //!__CHARACTER_CONTROLLER_WRAPER__