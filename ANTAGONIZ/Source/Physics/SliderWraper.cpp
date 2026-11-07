#include "SliderWraper.hpp"
#include "CommandQueue.hpp"
#include "SliderBody.hpp"
#include "RigidWraper.hpp"

namespace Ge
{
	SliderWraper::SliderWraper(SliderBody* sb, CommandQueue* queue)
	{
		m_sb = sb;
		m_queue = queue;
	}

	void SliderWraper::setSpring(RigidWraper* bodyA, RigidWraper* bodyB,const glm::quat dir,glm::vec3 limit)
	{
		MethodCommand<SliderBody,RigidBody*, RigidBody*, const glm::quat, glm::vec3>* command = new MethodCommand<SliderBody, RigidBody*, RigidBody*, const glm::quat, glm::vec3>(m_sb, &SliderBody::setSpring,bodyA->getRigidBody(), bodyB->getRigidBody(),dir,limit);
		m_queue->push((Command*)command);
	}

	void SliderWraper::updateSpring(float stiffness, float damping)
	{
		MethodCommand<SliderBody, float, float>* command = new MethodCommand<SliderBody,float, float>(m_sb, &SliderBody::updateSpring,stiffness,damping);
		m_queue->push((Command*)command);
	}

	void SliderWraper::setPull(float maxForce, RigidWraper* body, const glm::quat dir)
	{
		MethodCommand<SliderBody, float, RigidBody*, const glm::quat>* command = new MethodCommand<SliderBody, float, RigidBody*, const glm::quat>(m_sb, &SliderBody::setPull, maxForce, body->getRigidBody(), dir);
		m_queue->push((Command*)command);
	}

	void SliderWraper::updateTarget(const glm::vec3 worldTarget)
	{
		MethodCommand<SliderBody, const glm::vec3>* command = new MethodCommand<SliderBody, const glm::vec3>(m_sb, &SliderBody::updateTarget, worldTarget);
		m_queue->push((Command*)command);
	}

	void SliderWraper::disable()
	{
		MethodCommand<SliderBody>* command = new MethodCommand<SliderBody>(m_sb, &SliderBody::disable);
		m_queue->push((Command*)command);
	}

	bool SliderWraper::isActive() const
	{
		return m_sb->isActive();
	}

	ModeSlider SliderWraper::mode() const
	{
		return m_sb->mode();
	}
	SliderBody* SliderWraper::getSlider() const
	{
		return m_sb;
	}
}