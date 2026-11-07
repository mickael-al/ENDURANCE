#include "InputManager.hpp"
#include "GraphicsDataMisc.hpp"
#include "Debug.hpp"

namespace Ge
{
	bool InputManager::initialize(GraphicsDataMisc * gdm)
	{
		Debug::Info("Initialisation de l'InputManager");
		m_window = gdm->str_window;
		m_keys.fill(false); m_keyDown.fill(false); m_keyUp.fill(false);
		m_mouse.fill(false); m_mouseDown.fill(false); m_mouseUp.fill(false);
		glfwGetCursorPos(m_window, &m_lastMousePos.x, &m_lastMousePos.y);
		return true;
	}

	void InputManager::updateAxis()
	{
		if (m_window == nullptr) return;
		for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
		{
			const bool held = glfwGetKey(m_window, key) == GLFW_PRESS;
			m_keyDown[key] = held && !m_keys[key];
			m_keyUp[key] = !held && m_keys[key];
			m_keys[key] = held;
		}
		for (int key = 0; key <= GLFW_MOUSE_BUTTON_LAST; ++key)
		{
			const bool held = glfwGetMouseButton(m_window, key) == GLFW_PRESS;
			m_mouseDown[key] = held && !m_mouse[key];
			m_mouseUp[key] = !held && m_mouse[key];
			m_mouse[key] = held;
		}
		glfwGetCursorPos(m_window, &m_Mpos.x, &m_Mpos.y);
		m_axisMouse = m_Mpos - m_lastMousePos;
		m_lastMousePos = m_Mpos;
	}

	void InputManager::release()
	{
		Debug::Info("Liberation de l'InputManager");
	}

	const char * InputManager::getGamepadName(int jid) const
	{
		return glfwGetGamepadName(jid);
	}

	bool InputManager::getGamepadState(int jid, int key) const
	{
		if (key < 0 || key > GLFW_GAMEPAD_BUTTON_LAST)
		{
			return false;
		}
		GLFWgamepadstate state{};
		if (glfwGetGamepadState(jid, &state) != GLFW_TRUE)
		{
			return false;
		}
		return state.buttons[key] == GLFW_PRESS;
	}

	float InputManager::getGamepadAxis(int jid, int indice) const
	{
		if (indice < 0 || indice > GLFW_GAMEPAD_AXIS_LAST)
		{
			Debug::Error("l'indice de GetGamepadAxis n'existe pas");
			return 0.0f;
		}
		GLFWgamepadstate state{};
		if (glfwGetGamepadState(jid, &state) != GLFW_TRUE)
		{
			return 0.0f;
		}
		return state.axes[indice];
	}

	bool InputManager::getKey(int key) const
	{
		return key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST && m_keys[key];
	}
	bool InputManager::getKeyDown(int key)
	{
		return key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST && m_keyDown[key];
	}
	bool InputManager::getKeyUp(int key)
	{
		return key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST && m_keyUp[key];
	}
	bool InputManager::getMouse(int key, int pressType) const
	{
		return key >= 0 && key <= GLFW_MOUSE_BUTTON_LAST &&
			(pressType == GLFW_PRESS ? m_mouse[key] : !m_mouse[key]);
	}
	bool InputManager::getMouseDown(int key)
	{
		return key >= 0 && key <= GLFW_MOUSE_BUTTON_LAST && m_mouseDown[key];
	}
	bool InputManager::getMouseUp(int key)
	{
		return key >= 0 && key <= GLFW_MOUSE_BUTTON_LAST && m_mouseUp[key];
	}

	void InputManager::registerScrollCallback(GLFWscrollfun callback)
	{
		glfwSetScrollCallback(m_window, callback);
	}

	void InputManager::HideMouse(bool state)
	{
		glfwSetInputMode(m_window, GLFW_CURSOR, state ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		glfwGetCursorPos(m_window, &m_lastMousePos.x, &m_lastMousePos.y);
		m_Mpos = m_lastMousePos;
		m_axisMouse = glm::tvec2<double>(0.0);
	}

	double InputManager::axisMouseX()
	{
		return m_axisMouse.x;
	}

	double InputManager::axisMouseY()
	{
		return m_axisMouse.y;
	}

	double InputManager::getMousePosX() const
	{
		return m_Mpos.x;
	}

	double InputManager::getMousePosY() const
	{
		return m_Mpos.y;
	}
}
