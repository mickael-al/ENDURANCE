#include "FlyCamera.hpp"
#include "GraphicsDataMisc.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "InputManager.hpp"
#include "Camera.hpp"

namespace Ge
{
	FlyCamera::FlyCamera(GraphicsDataMisc * gdm) : Camera(gdm,0)
	{
		m_im = Engine::getPtrClass().inputManager;
		m_angleY = 0.0f;
		m_angleX = 0.0f;
		m_moveSpeed = 0.0f;
		m_priority = 1.0f;
	}

	void FlyCamera::updateCamera()
	{
		if (m_lock) return;
		const float dt = Time::GetDeltaTime();
		const float previousBoost = m_addMoveSpeed;
		if (m_im->getKey(340))
		{
			// The boost is a velocity, not an extra distance on every frame.
			m_addMoveSpeed += dt * m_fastSpeedMult;
			m_moveSpeed = (m_maxSpeed + 0.5f * (previousBoost + m_addMoveSpeed)) * dt;
		}
		else
		{
			m_addMoveSpeed = 0.0f;
			m_moveSpeed = dt * m_maxSpeed;
		}

		bool changed = false;
		if (m_im->getMouse(1))
		{
			const float mouseX = static_cast<float>(m_im->axisMouseX());
			const float mouseY = static_cast<float>(m_im->axisMouseY());
			if (mouseX != 0.0f || mouseY != 0.0f)
			{
				m_angleY -= mouseY * m_maxSpeedRotate;
				m_angleX -= mouseX * m_maxSpeedRotate;
				m_eulerAngles = glm::vec3(m_angleY, m_angleX, 0.0f);
				m_transform.rotation = glm::quat(glm::radians(m_eulerAngles));
				changed = true;
			}
		}

		glm::vec3 movement(0.0f);
		if (m_im->getKey(87))
			movement += transformDirectionAxeZ() * m_moveSpeed;
		else if (m_im->getKey(83))
			movement -= transformDirectionAxeZ() * m_moveSpeed;
		if (m_im->getKey(68))
			movement -= transformDirectionAxeX() * m_moveSpeed;
		else if (m_im->getKey(65))
			movement += transformDirectionAxeX() * m_moveSpeed;
		if (m_im->getKey(341))
			movement.y -= m_moveSpeed;
		else if (m_im->getKey(32))
			movement.y += m_moveSpeed;
		if (movement.x != 0.0f || movement.y != 0.0f || movement.z != 0.0f)
		{
			m_transform.position += movement;
			changed = true;
		}
		// Preserve GObject's parent/local transform and child propagation, but
		// upload the final camera pose and rebuild shadow matrices only once.
		if (changed) updateLocal();
	}

	glm::vec2 FlyCamera::getAngle()
	{		
		return glm::vec2(m_angleX, m_angleY);
	}

	void FlyCamera::setAngle(glm::vec2 angle)
	{
		m_angleX = angle.x;
		m_angleY = angle.y;
	}

	void FlyCamera::setLock(bool lock)
	{
		m_lock = lock;
	}

	bool FlyCamera::getLock() const
	{
		return m_lock;
	}
}