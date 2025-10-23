#include "window.hpp"
#include <nfd/nfd_glfw3.h>

bool Window::shouldClose() {
	return glfwWindowShouldClose(m_window);
}
int Window::getButton(int button) {
	if(button < GLFW_KEY_SPACE) {
		return glfwGetMouseButton(m_window, button);
	}
	else {
		return glfwGetKey(m_window, button);
	}
}

void Window::getCursorPos(f64* x, f64* y) {
	glfwGetCursorPos(m_window, x, y);
}

void Window::centerCursor(){ 
	glfwSetCursorPos(m_window, m_width / 2.0, m_height / 2.0);
}

void Window::showCursor(){ 
	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void Window::hideCursor(){ 
	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Window::updateSize() {
	glfwGetFramebufferSize(m_window, &m_width, &m_height);
	while(m_width == 0 || m_height == 0) {
		glfwGetFramebufferSize(m_window, &m_width, &m_height);
		glfwWaitEvents();
	}
}

u32 Window::width(){ 
	return static_cast<u32>(m_width);
}
u32 Window::height(){ 
	return static_cast<u32>(m_height);
}

GLFWwindow* Window::window() {
		return m_window;
}

nfdwindowhandle_t Window::nfdHandle() {
	return m_nativeHandle;
}

Window::Window() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	m_width = mode->width * 3 / 4;
	m_height = mode->height * 3 / 4;

	m_window = glfwCreateWindow(m_width, m_height, "Capstone", nullptr, nullptr);

	NFD_Init();
	NFD_GetNativeWindowFromGLFWWindow(m_window, &m_nativeHandle);
}

Window::~Window() {
	NFD_Quit();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}