#ifndef __ENGINE_INPUT_MANAGER__
#define __ENGINE_INPUT_MANAGER__

#include <array>
#include "Initializer.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
struct GraphicsDataMisc;
class GLFWwindow;

namespace Ge
{
	using GLFWscrollfun = void (*)(GLFWwindow*, double, double);
	class InputManager final : InitializerAPI
	{
	public:
		bool initialize(GraphicsDataMisc * gdm);
		void release();
	private:
		friend class Engine;
		void updateAxis();
	public:
		const char * getGamepadName(int jid) const;
		bool getGamepadState(int jid,int key) const;
		float getGamepadAxis(int jid, int indice) const;
		bool getKey(int key) const;
		bool getKeyUp(int key);
		bool getKeyDown(int key);
		bool getMouse(int key, int pressType = GLFW_PRESS) const;
		bool getMouseDown(int key);
		bool getMouseUp(int key);
		void registerScrollCallback(GLFWscrollfun callback);
		void HideMouse(bool state);
		double axisMouseX();
		double axisMouseY();
		double getMousePosX() const;
		double getMousePosY() const;
	private:
		// Poll once per frame: every consumer sees the same edge, including after
		// menus stopped querying an action for several frames.
		std::array<bool, GLFW_KEY_LAST + 1> m_keys{}, m_keyDown{}, m_keyUp{};
		std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_mouse{}, m_mouseDown{}, m_mouseUp{};
		glm::tvec2<double> m_lastMousePos{ 0.0 };
		glm::tvec2<double> m_axisMouse{ 0.0 };
		glm::tvec2<double> m_Mpos{ 0.0 };
		GLFWwindow * m_window = nullptr;
	};
}

#endif //!__ENGINE_INPUT_MANAGER__
