#include "Window.hpp"
#include "Debug.hpp"
#include "GLFW/glfw3.h"
#include "GraphicsDataMisc.hpp"
#include "stb-cmake/stb_image.h"
#include "Engine.hpp"
#include "EngineHeader.hpp"
#include "FrameBuffer.hpp"
#include "PostProcess.hpp"

namespace Ge
{
	Window::Window(FrameBuffer* fb, PostProcess * pp)
	{
		m_fb = fb;
		m_pp = pp;
	}

	bool Window::initialize(uint32_t Width, uint32_t Height, const char * name, const char * iconPath,bool vsync, GraphicsDataMisc * gdm)
	{
		if (!glfwInit())
		{
			return false;
		}
		m_gdm = gdm;
		gdm->str_width = Width;
		gdm->str_height = Height;
#ifdef OPENGL_DEBUG
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
		this->m_window = glfwCreateWindow(Width, Height, name, NULL, NULL);
		if (!this->m_window)
		{
			glfwTerminate();
			Debug::Error("glfwCreateWindow");
			return false;
		}


		glfwSetWindowUserPointer(this->m_window, this);
		glfwSetFramebufferSizeCallback(this->m_window, this->framebufferResizeCallback);
		gdm->str_window = this->m_window;
		glfwMakeContextCurrent(this->m_window);
		if (!vsync)
		{
			glfwSwapInterval(0);//VSYNC
		}
		Debug::INITSUCCESS("Glfw Window");
		if (iconPath && iconPath[0])
		{
			GLFWimage images;
			images.pixels = stbi_load(iconPath, &images.width, &images.height, 0, 4);
			glfwSetWindowIcon(this->m_window, 1, &images);
			stbi_image_free(images.pixels);
		}

		centerWindow(Width, Height);

		return true;
	}

	void Window::release()
	{
		glfwDestroyWindow(this->m_window);/*DestroyWindow*/
		glfwTerminate();
		Debug::RELEASESUCCESS("Window");
	}

	void Window::centerWindow(int width, int height)
	{
		GLFWmonitor* primary = glfwGetPrimaryMonitor();
		if (!primary || !this->m_window) { return; }

		const GLFWvidmode* vidMode = glfwGetVideoMode(primary);
		if (!vidMode) return;

		int screenWidth = vidMode->width;
		int screenHeight = vidMode->height;

		int x = (screenWidth - width) / 2;
		int y = (screenHeight - height) / 2;

		glfwSetWindowPos(m_window, x, y);
	}

	void Window::setWindowPosition(int x, int y)
	{
		glfwSetWindowPos(m_window, x, y);
	}

	void Window::getWindowPosition(int* x, int* y)
	{
		glfwGetWindowPos(m_window, x, y);
	}

	bool Window::toggleFullscreen()
	{
		return setFullscreen(!m_fullscreen);
	}

	bool Window::setFullscreen(bool fullscreen)
	{
		if (!m_window)
		{
			return false;
		}
		if (fullscreen == m_fullscreen)
		{
			return m_fullscreen;
		}

		m_fullscreen = fullscreen;
		if (m_fullscreen)
		{
			glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
			glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);

			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);

			glfwSetWindowMonitor(
				m_window,
				monitor,
				0, 0,
				mode->width,
				mode->height,
				mode->refreshRate
			);
		}
		else
		{
			glfwSetWindowMonitor(
				m_window,
				nullptr,
				m_windowedX,
				m_windowedY,
				m_windowedWidth,
				m_windowedHeight,
				0
			);

		}
		return m_fullscreen;
	}


	void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		if (width == 0 && height == 0)
		{
			return;
		}
		win->m_gdm->str_width = width;
		win->m_gdm->str_height = height;
		win->m_framebufferResized = true;
		ptrClass pc = Engine::getPtrClass();
		pc.cameraManager->updateStorage();
		pc.settingManager->setWindowHeight(height);
		pc.settingManager->setWindowWidth(width);
		win->m_fb->resize(width, height);
		win->m_pp->resize(width, height);
		Debug::Info("Redimension de la fenetre : %d , %d", width, height);
	}

	void Window::setframebufferResized(bool state)
	{
		m_framebufferResized = state;
	}

	bool Window::getframebufferResized()
	{
		return m_framebufferResized;
	}

	GLFWwindow * Window::getWindow()
	{
		return m_window;
	}
}
