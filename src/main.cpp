#include "renderer.hpp"

#ifdef NDEBUG
#define main WinMain
#endif 


int main() {
	std::string executablePath(__argv[0]);
	std::filesystem::current_path(executablePath.substr(0, executablePath.find_last_of('\\')));

	Renderer(__argc > 1 ? __argv[1] : nullptr).run();
}