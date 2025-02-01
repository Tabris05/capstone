#ifndef RENDERER_H
#define RENDERER_H

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <glfw/glfw3.h>

class Renderer {
	public:
		Renderer();
		~Renderer();
	
		Renderer(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
	
		Renderer& operator=(const Renderer&) = delete;
		Renderer& operator=(Renderer&&) = delete;
	
		void run();
		void onResize();

	private:
		i32 m_width;
		i32 m_height;
		GLFWwindow* m_window;
};

#endif
