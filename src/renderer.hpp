#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
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
		static constexpr VkFormat m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		static constexpr VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

		struct Image {
			VkDeviceMemory memory;
			VkImage image;
			VkImageView view;
		};

		struct Buffer {
			VkDeviceMemory memory;
			VkBuffer buffer;
			union {
				void* hostPtr;
				VkDeviceAddress devicePtr;
			};
		};

		struct PushConstants {
			VkDeviceAddress vertexBuffer;
			glm::mat4 transform;
		};

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
		b8 m_swapchainDirty = false;

		VkInstance m_instance;
		VkPhysicalDevice m_physicalDevice;
		VkPhysicalDeviceMemoryProperties m_memProps;
		u32 m_graphicsQueueFamily;
		VkQueue m_graphicsQueue;
		u32 m_transferQueueFamily;
		VkQueue m_transferQueue;
		VkDevice m_device;
		VkSurfaceKHR m_surface;
		VkSurfaceFormatKHR m_surfaceFormat;
		VkSwapchainKHR m_swapchain;
		std::vector<VkImage> m_swapchainImages;
		VkPipelineLayout m_pipelineLayout;
		VkPipeline m_pipeline;
		Image m_colorTarget;
		Image m_depthTarget;
		Buffer m_vertexBuffer;
		Buffer m_indexBuffer;

		f32 m_fov = 90.0f;
		glm::vec3 m_position = { 0.0f, 0.0f, -2.0f };

		u32 getQueue(VkQueueFlags include, VkQueueFlags exclude = 0);
		u32 getMemoryIndex(VkMemoryPropertyFlags flags, u32 mask);
		std::vector<u32> getShaderSource(const char* path);
		void createSwapchain();
		void recreateSwapchain();

		Image createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage);
		void destroyImage(Image image);

		Buffer createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
		void destroyBuffer(Buffer buffer);
};

#endif
