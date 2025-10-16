#include "renderer.hpp"

#ifdef NDEBUG
#define main WinMain
#endif 


int main() {
	Renderer(__argc > 1 ? __argv[1] : nullptr).run();
}