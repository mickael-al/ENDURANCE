#ifndef __ENGINE_FLY_CAMERA__
#define __ENGINE_FLY_CAMERA__

#include "Camera.hpp"

struct GraphicsDataMisc;
namespace Ge
{
	class InputManager;
}

namespace Ge
{
	class FlyCamera : public Camera
	{
	public:
		FlyCamera(GraphicsDataMisc * gdm);
		void updateCamera();
		glm::vec2 getAngle();
		void setAngle(glm::vec2 angle);
		void setLock(bool lock);
		bool getLock() const;
	private:	
		InputManager * m_im;
		bool m_lock = false;
		float m_angleX = 0.0f;
		float m_angleY = 0.0f;
		float m_maxSpeed = 10.0f;
		float m_fastSpeedMult = 1.0f;
		float m_moveSpeed = 0.0f;
		float m_maxSpeedRotate = 0.2f;
		float m_addMoveSpeed = 0.0f;
	};
}

#endif //!__ENGINE_FLY_CAMERA__