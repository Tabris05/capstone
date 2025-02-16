#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <filesystem>
#include <limits>

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

		struct AABB {
			glm::vec3 min{ std::numeric_limits<f32>::infinity() };
			glm::vec3 max{ -std::numeric_limits<f32>::infinity() };
		};

		struct Image {
			VkDeviceMemory memory = nullptr;
			VkImage image = nullptr;
			VkImageView view = nullptr;
		};

		struct Buffer {
			VkDeviceMemory memory = nullptr;
			VkBuffer buffer = nullptr;
			union {
				void* hostPtr = nullptr;
				VkDeviceAddress devicePtr;
			};
		};

		struct Model {
			std::vector<Image> images;
			std::vector<VkSampler> samplers;
			Buffer vertexBuffer;
			Buffer indexBuffer;
			Buffer indirectBuffer;
			glm::mat4 baseTransform;
			AABB aabb;
			u64 numDrawCommands;
		};

		struct PushConstants {
			VkDeviceAddress vertexBuffer;
			glm::mat4 transform;
			glm::mat3 normalTransform;
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

		VkInstance m_instance = {};
		VkPhysicalDevice m_physicalDevice = {};
		VkPhysicalDeviceMemoryProperties m_memProps;
		VkDevice m_device;

		u32 m_graphicsQueueFamily;
		u32 m_computeQueueFamily;
		u32 m_transferQueueFamily;
		VkQueue m_graphicsQueue = {};
		VkQueue m_computeQueue = {};
		VkQueue m_transferQueue = {};

		VkSurfaceKHR m_surface = {};
		VkSurfaceFormatKHR m_surfaceFormat;
		VkSwapchainKHR m_swapchain = {};
		std::vector<VkImage> m_swapchainImages;

		VkPipelineLayout m_modelPipelineLayout = {};
		VkPipeline m_modelPipeline = {};
		
		VkCommandPool m_transferPool;
		VkCommandBuffer m_transferCmd;

		VkCommandPool m_computePool;
		VkCommandBuffer m_computeCmd;

		VkSemaphore m_mipSem;

		VkDescriptorSetLayout m_srgbSetLayout;
		VkPipelineLayout m_srgbPipelineLayout;
		VkPipeline m_srgbPipeline;

		VkDescriptorSetLayout m_mipSetLayout;
		VkPipelineLayout m_mipPipelineLayout;
		VkPipeline m_mipPipeline;

		Image m_colorTarget;
		Image m_depthTarget;
		Model m_model;

		f32 m_fov = 90.0f;
		glm::vec3 m_position{ 0.0f, 0.0f, -2.0f };

		u32 getQueue(VkQueueFlags include, VkQueueFlags exclude = 0);
		u32 getMemoryIndex(VkMemoryPropertyFlags flags, u32 mask);
		std::vector<u32> getShaderSource(const char* path);
		void createSwapchain();
		void recreateSwapchain();

		Image createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips = 1);
		void destroyImage(Image image);

		Buffer createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
		void destroyBuffer(Buffer buffer);

		void createModel(std::filesystem::path path);
		void destroyModel(Model model);
};

#endif
