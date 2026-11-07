#include "CharacterControllerWraper.hpp"
#include "CommandQueue.hpp"
#include "CharacterController.hpp"

namespace Ge
{

	CharacterControllerWraper::CharacterControllerWraper(CharacterController* cc, CommandQueue* queue)
	{
		m_cc = cc;
		m_queue = queue;
	}

	void CharacterControllerWraper::setPosition(glm::vec3 pos)
	{
		MethodCommand<CharacterController, glm::vec3>* command = new MethodCommand<CharacterController, glm::vec3>(m_cc, &CharacterController::setPosition, pos);
		m_queue->push((Command*)command);
	}

	glm::vec3 CharacterControllerWraper::getPosition() const
	{
		return m_cc->getPosition();
	}

	void CharacterControllerWraper::setLinearVelocity(glm::vec3 vel)
	{
		MethodCommand<CharacterController, glm::vec3>* command = new MethodCommand<CharacterController, glm::vec3>(m_cc, &CharacterController::setLinearVelocity, vel);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::applyImpulse(glm::vec3 vel)
	{
		MethodCommand<CharacterController, glm::vec3>* command = new MethodCommand<CharacterController, glm::vec3>(m_cc, &CharacterController::applyImpulse, vel);
		m_queue->push((Command*)command);
	}


	glm::vec3 CharacterControllerWraper::getLinearVelocity() const
	{
		return m_cc->getLinearVelocity();
	}

	void CharacterControllerWraper::setWalkDirection(glm::vec3 dir)
	{
		MethodCommand<CharacterController, glm::vec3>* command = new MethodCommand<CharacterController, glm::vec3>(m_cc, &CharacterController::setWalkDirection, dir);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setMovementVelocity(glm::vec3 velocity)
	{
		MethodCommand<CharacterController, glm::vec3>* command =
			new MethodCommand<CharacterController, glm::vec3>(
				m_cc,
				&CharacterController::setMovementVelocity,
				velocity);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setCapsuleCylinderHeight(float height)
	{
		MethodCommand<CharacterController, float>* command =
			new MethodCommand<CharacterController, float>(
				m_cc,
				&CharacterController::setCapsuleCylinderHeight,
				height);
		m_queue->push((Command*)command);
	}

	bool CharacterControllerWraper::canStand() const
	{
		return m_cc->canStand();
	}

	void CharacterControllerWraper::jump()
	{
		MethodCommand<CharacterController>* command = new MethodCommand<CharacterController>(m_cc, &CharacterController::jump);
		m_queue->push((Command*)command);
	}

	bool CharacterControllerWraper::onGround() const
	{
		return m_cc->onGround();
	}

	void CharacterControllerWraper::setGravity(glm::vec3 gravity)
	{
		MethodCommand<CharacterController, glm::vec3>* command = new MethodCommand<CharacterController, glm::vec3>(m_cc, &CharacterController::setGravity, gravity);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setMaxSlope(float degres)
	{
		MethodCommand<CharacterController, float>* command = new MethodCommand<CharacterController, float>(m_cc, &CharacterController::setMaxSlope, degres);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setWarp(glm::vec3 pos)
	{
		MethodCommand<CharacterController, glm::vec3>* command = new MethodCommand<CharacterController, glm::vec3>(m_cc, &CharacterController::setWarp, pos);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setFallSpeed(float speed)
	{
		MethodCommand<CharacterController, float>* command = new MethodCommand<CharacterController, float>(m_cc, &CharacterController::setFallSpeed, speed);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setJumpSpeed(float speed)
	{
		MethodCommand<CharacterController, float>* command = new MethodCommand<CharacterController, float>(m_cc, &CharacterController::setJumpSpeed, speed);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setMaxJumpHeight(float height)
	{
		MethodCommand<CharacterController, float>* command = new MethodCommand<CharacterController, float>(m_cc, &CharacterController::setMaxJumpHeight, height);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setStepHeight(float height)
	{
		MethodCommand<CharacterController, float>* command = new MethodCommand<CharacterController, float>(m_cc, &CharacterController::setStepHeight, height);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setUpInterpolate(bool state)
	{
		MethodCommand<CharacterController, bool>* command = new MethodCommand<CharacterController, bool>(m_cc, &CharacterController::setUpInterpolate, state);
		m_queue->push((Command*)command);
	}

	void CharacterControllerWraper::setActionStatus(bool state)
	{
		MethodCommand<CharacterController, bool>* command = new MethodCommand<CharacterController, bool>(m_cc, &CharacterController::setActionStatus, state);
		m_queue->push((Command*)command);
	}

	bool CharacterControllerWraper::canJump()
	{
		return m_cc->canJump();
	}

	CharacterController* CharacterControllerWraper::getCharacterController() const
	{
		return m_cc;
	}
}