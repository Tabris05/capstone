#ifndef RENDERER_H
#define RENDERER_H

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <glfw/glfw3.h>
#include <vector>

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
		static constexpr u8 m_framesInFlight = 2;

		struct {
			VkCommandPool cmdPool;
			VkCommandBuffer cmdBuffer;
			VkSemaphore acquireSem;
			VkSemaphore presentSem;
			VkFence fence;
		} m_perFrameData[m_framesInFlight];

		i32 m_width;
		i32 m_height;
		GLFWwindow* m_window;

		u8 m_frameIndex = 0;

		VkInstance m_instance;
		VkPhysicalDevice m_physicalDevice;
		VkPhysicalDeviceMemoryProperties m_memProps;
		u32 m_graphicsQueueFamily;
		VkQueue m_graphicsQueue;
		VkDevice m_device;
		VkSurfaceKHR m_surface;
		VkSurfaceFormatKHR m_surfaceFormat;
		VkSwapchainKHR m_swapchain;
		std::vector<VkImage> m_swapchainImages;

		u32 getQueue(VkQueueFlags include, VkQueueFlags exclude = 0);
		void createSwapchain();
};

#endif
