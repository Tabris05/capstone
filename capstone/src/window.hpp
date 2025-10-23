#ifndef WINDOW_H
#define WINDOW_H

#include <tbrs/types.hpp>
#include <glfw/glfw3.h>
#include <nfd/nfd.h>

class Window {
public:
	bool shouldClose();
	int getButton(int button);
	void getCursorPos(f64* x, f64* y);
	void centerCursor();
	void showCursor();
	void hideCursor();
	void updateSize();

	u32 width();
	u32 height();
	GLFWwindow* window();
	nfdwindowhandle_t nfdHandle();

	Window();
	~Window();

private:
	i32 m_width;
	i32 m_height;
	GLFWwindow* m_window;
	nfdwindowhandle_t m_nativeHandle;
};

#endif