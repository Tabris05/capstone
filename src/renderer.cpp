#include "renderer.h"

Renderer::Renderer() {
	// glfw
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
		m_width = mode->width * 3 / 4;
		m_height = mode->height * 3 / 4;

		m_window = glfwCreateWindow(m_width, m_height, "Capstone", nullptr, nullptr);

		glfwSetWindowUserPointer(m_window, this);
		glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, i32 width, i32 height) {
			reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window))->onResize();
		});
	}
}

Renderer::~Renderer() {
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

void Renderer::run() {
	while(!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
	}
}

void Renderer::onResize() {

}