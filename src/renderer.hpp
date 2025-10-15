#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <fastgltf/types.hpp>
#include <nfd/nfd.h>
#include <imgui/imgui.h>
#include <future>
#include <queue>

class Renderer {
	public:
		Renderer(const char* path);
		~Renderer();
	
		Renderer(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
	
		Renderer& operator=(const Renderer&) = delete;
		Renderer& operator=(Renderer&&) = delete;
	
		void run();
		void onResize();
		void onScroll(f32 scroll);

	private:
		static constexpr u8 m_framesInFlight = 2;
		static constexpr u32 m_maxBloomMips = 6;
		static constexpr u32 m_irradianceMapSize = 32;
		static constexpr u32 m_brdfIntegralLUTSize = 1024;
		static constexpr u32 m_shadowMapSize = 2048; // this is hardcoded in shadow.vert and pbr.glsl
		static constexpr u32 m_poissonDiskWindowSize = 8; // this is hardcoded in pbr.glsl
		static constexpr u32 m_poissonDiskFilterSize = 9; // this is hardcoded in pbr.glsl
		static constexpr f32 m_maxPitch = 89.0f; // in deg
		static constexpr VkFormat m_colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		static constexpr VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
		static constexpr VkFormat m_uiFormat = VK_FORMAT_R8G8B8A8_UNORM;

		static const inline std::unordered_map<fastgltf::Filter, VkFilter> m_filterMap = {
			{ fastgltf::Filter::Nearest, VK_FILTER_NEAREST },
			{ fastgltf::Filter::Linear, VK_FILTER_LINEAR },
			{ fastgltf::Filter::NearestMipMapNearest, VK_FILTER_NEAREST },
			{ fastgltf::Filter::LinearMipMapNearest, VK_FILTER_LINEAR },
			{ fastgltf::Filter::NearestMipMapLinear, VK_FILTER_NEAREST },
			{ fastgltf::Filter::LinearMipMapLinear, VK_FILTER_LINEAR },
		};

		static const inline std::unordered_map<fastgltf::Wrap, VkSamplerAddressMode> m_wrapMap = {
			{ fastgltf::Wrap::ClampToEdge, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
			{ fastgltf::Wrap::Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT },
			{ fastgltf::Wrap::MirroredRepeat, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT }
		};

		struct AABB {
			glm::vec3 min{ std::numeric_limits<f32>::infinity() };
			glm::vec3 max{ -std::numeric_limits<f32>::infinity() };
		};

		struct Image {
			VkDeviceMemory memory = {};
			VkImage image = {};
			VkImageView view = {};
		};

		struct Buffer {
			VkDeviceMemory memory = {};
			VkBuffer buffer = {};
			union {
				void* hostPtr = nullptr;
				VkDeviceAddress devicePtr;
			};
		};

		struct Model {
			std::vector<Image> images;
			std::vector<VkSampler> samplers;
			VkDescriptorPool texPool = {};
			VkDescriptorSet texSet = {};
			Buffer materialBuffer;
			Buffer vertexBuffer;
			Buffer indexBuffer;
			Buffer indirectBuffer;
			glm::mat4 baseTransform;
			AABB aabb;
			u64 numOpaqueDrawCommands = 0;
			u64 numBlendDrawCommands = 0;
		};

		struct Skybox {
			Image environmentMap;
			Image irradianceMap;
			Image radianceMap;
		};

		struct PushConstants {
			VkDeviceAddress oitBuffer;
			VkDeviceAddress vertexBuffer;
			VkDeviceAddress materialBuffer;
			VkDeviceAddress poissonDiskBuffer;
			glm::mat4 cameraTransform;
			glm::mat4 lightTransform;
			glm::mat4x3 modelTransform;
			glm::vec4 lightColor;
			glm::vec3 camPos;
			glm::vec3 lightAngle;
			u32 frameBufferWidth;
		};

		struct {
			VkCommandPool cmdPool;
			VkCommandBuffer cmdBuffer;
			VkSemaphore acquireSem;
			VkFence fence;
			std::queue<std::function<void(void)>> deletionQueue;
		} m_perFrameData[m_framesInFlight];

		// window
		i32 m_width;
		i32 m_height;
		GLFWwindow* m_window;
		nfdwindowhandle_t m_nativeHandle;

		// vulkan
		u8 m_frameIndex = 0;
		b8 m_swapchainDirty = false;

		VkInstance m_instance = {};
		VkPhysicalDevice m_physicalDevice = {};
		VkPhysicalDeviceMemoryProperties m_memProps;
		u32 m_maxSampledImageDescriptors;
		VkDevice m_device;

		u32 m_graphicsQueueFamily;
		u32 m_computeQueueFamily;
		u32 m_transferQueueFamily;
		VkQueue m_graphicsQueue = {};
		VkQueue m_computeQueue = {};
		VkQueue m_transferQueue = {};

		VkSurfaceKHR m_surface = {};
		VkSurfaceFormatKHR m_surfaceFormat;
		VkPresentModeKHR m_nonVsyncPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		VkSwapchainKHR m_swapchain = {};
		std::vector<VkImage> m_swapchainImages;
		std::vector<VkImageView> m_swapchainImageViews;
		std::vector<VkSemaphore> m_swapchainSems;
		std::vector<std::pair<VkImageView, glm::uvec2>> m_bloomMips;

		VkDescriptorSetLayout m_modelSetLayout = {};
		VkDescriptorSetLayout m_modelPushDescriptorLayout = {};
		VkPipelineLayout m_modelPipelineLayout = {};
		VkPipeline m_prepassPipeline = {};
		VkPipeline m_shadowPipeline = {};
		VkPipeline m_opaquePipeline = {};
		VkPipeline m_blendPipeline = {};

		VkDescriptorSetLayout m_skyboxSetLayout = {};
		VkPipelineLayout m_skyboxPipelineLayout = {};
		VkPipeline m_skyboxPipeline = {};
		
		VkCommandPool m_transferPoolModelThread = {};
		VkCommandBuffer m_transferCmdModelThread = {};
		VkCommandPool m_transferPoolSkyboxThread = {};
		VkCommandBuffer m_transferCmdSkyboxThread = {};

		VkCommandPool m_computePoolModelThread = {};
		VkCommandBuffer m_computeCmdModelThread = {};
		VkCommandPool m_computePoolSkyboxThread = {};
		VkCommandBuffer m_computeCmdSkyboxThread = {};

		VkSemaphore m_transferToComputeSemModelThread = {};
		VkSemaphore m_transferToComputeSemSkyboxThread = {};

		VkFence m_transferFenceModelThread = {};
		VkFence m_computeFenceModelThread = {};
		VkFence m_transferFenceSkyboxThread = {};
		VkFence m_computeFenceSkyboxThread = {};

		VkDescriptorSetLayout m_oneImageSetLayout = {};
		VkPipelineLayout m_oneImagePipelineLayout = {};
		VkPipelineLayout m_transparencyCompositePipelineLayout = {};

		VkDescriptorSetLayout m_twoImageSetLayout = {};
		VkPipelineLayout m_twoImagePipelineLayout = {};

		VkDescriptorSetLayout m_oneTexOneImageSetLayout = {};
		VkPipelineLayout m_oneTexOneImagePipelineLayout = {};
		VkPipelineLayout m_bloomPipelineLayout = {};
		
		VkDescriptorSetLayout m_threeImageSetLayout = {};
		VkPipelineLayout m_postprocessingPipelineLayout = {};

		VkPipeline m_mipPipeline = {};
		VkPipeline m_srgbMipPipeline = {};
		VkPipeline m_cubePipeline = {};
		VkPipeline m_cubeMipPipeline = {};
		VkPipeline m_irradiancePipeline = {};
		VkPipeline m_radiancePipeline = {};
		VkPipeline m_brdfIntegralPipeline = {};
		VkPipeline m_transparencyCompositePipeline = {};
		VkPipeline m_postprocessingPipeline = {};
		VkPipeline m_uiCompositePipeline = {};
		VkPipeline m_bloomDownsamplePipeline = {};
		VkPipeline m_bloomUpsamplePipeline = {};
		VkPipeline m_fxaaPipeline = {};
		VkPipeline m_blitPipeline = {};

		Buffer m_poissonDiskBuffer;
		Buffer m_oitBuffer;
		Image m_colorTarget;
		Image m_bloomTarget;
		Image m_depthTarget;
		Image m_uiTarget;
		Model m_model;

		Skybox m_skybox;
		Image m_brdfIntegralTex;
		Image m_shadowMap;
		VkSampler m_skyboxSampler = {};
		VkSampler m_shadowSampler = {};

		// asset loading
		b8 m_loadingModel = false;
		std::future<Model> m_modelFuture;

		b8 m_loadingSkybox = false;
		std::future<Skybox> m_skyboxFuture;

		b8 m_savingScene = false;
		std::future<void> m_savingSceneFuture;

		b8 m_loadingScene = false;
		std::future<void> m_loadingSceneFuture;

		std::mutex m_asyncComputeLock;
		std::mutex m_dmaTransferLock;

		// timing
		u32 m_framesThisSecond = 0;
		f32 m_fpsLastSecond = 0.0;
		f32 m_lastSecond = 0.0;

		// movement
		b8 m_firstClick = true;
		f32 m_speed = 1.44f;
		f32 m_scroll = 0.0f;
		glm::vec3 m_position{ 0.0f, 0.0f, 2.0f };
		glm::vec3 m_rotation{ 0.0f, 0.0f,  -1.0f };

		// ImGui
		b8 m_vsync = true;
		i32 m_camIdx = 0;
		i32 m_colorIdx = 1;
		i32 m_AAIdx = 1;
		f32 m_fov = 90.0f;
		f32 m_sensitivity = 0.5f;
		f32 m_scrollSensitivity = 0.5f;
		f32 m_lightPitch = 0.0f;
		f32 m_lightYaw = 0.0f;
		f32 m_modelPitch = 0.0f;
		f32 m_modelYaw = 0.0f;
		f32 m_modelRoll = 0.0f;
		f32 m_modelScale = 1.0f;
		glm::vec3 m_lightAngle{ 0.0f, 0.0f, 1.0f };
		glm::vec4 m_lightColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::string m_modelPath;
		std::string m_skyboxPath;

		ImFont* m_largeFont = nullptr;

		u32 getQueue(VkQueueFlags include, VkQueueFlags exclude = 0);
		u32 getMemoryIndex(VkMemoryPropertyFlags flags, u32 mask);
		std::vector<u32> getShaderSource(std::filesystem::path);
		void createSwapchain();
		void recreateSwapchain();

		Image createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips = 1, b8 cube = false);
		void destroyImage(Image image);

		Buffer createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
		void destroyBuffer(Buffer buffer);

		Model createModel(std::filesystem::path path);
		void destroyModel(Model model);

		Skybox createSkybox(std::filesystem::path path);
		void destroySkybox(Skybox skybox);

		VkPipeline createComputePipeline(VkPipelineLayout layout, std::filesystem::path shaderPath);
		VkPipeline createGraphicsPipeline(VkPipelineLayout layout, std::filesystem::path vsPath, std::filesystem::path fsPath, VkCullModeFlagBits cullMode, VkCompareOp compareOp, bool depthWrite, bool hasColorAttachment);

		void handleInput(f32 deltaTime);
		void render(f32 thisFrame);
};

#endif
