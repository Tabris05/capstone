#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <glm/glm.hpp>
#include <vector>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <fastgltf/types.hpp>
#include <imgui/imgui.h>
#include <future>
#include <queue>
#include <any>
#include "semaphore.hpp"
#include "context.hpp"
#include "buffer.hpp"
#include "image.hpp"
#include "descriptor_heap.hpp"
#include "window.hpp"
#include "image_handle.hpp"
#include "pipeline.hpp"

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
		static constexpr u32 m_poissonDiskBufferSize = m_poissonDiskWindowSize * m_poissonDiskWindowSize * m_poissonDiskFilterSize * m_poissonDiskFilterSize * sizeof(glm::vec2);
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

		struct Model {
			DescriptorHeap* heap = nullptr;
			std::vector<Image> images;
			std::vector<u32> imageHandles;
			std::vector<u32> samplerHandles;
			Buffer materialBuffer;
			Buffer vertexBuffer;
			Buffer indexBuffer;
			Buffer indirectBuffer;
			glm::mat4 baseTransform;
			AABB aabb;
			u64 numOpaqueDrawCommands = 0;
			u64 numBlendDrawCommands = 0;

			Model() = default;

			Model(Model&& src) {
				memcpy(this, &src, sizeof(Model));
				memset(&src, 0, sizeof(Model));
			}

			Model& operator=(Model&& src) {
				this->~Model();
				new (this) Model(std::move(src)); return *this;
			};

			~Model() {
				if(heap) {
					for(u32 handle : imageHandles) {
						heap->freeImageHandle(handle);
					}
					for(u32 handle : samplerHandles) {
						heap->freeSamplerHandle(handle);
					}
				}
			}
		};

		struct Skybox {
			DescriptorHeap* heap = nullptr;
			Image environmentMap;
			u32 environmentMapHandle = ~0u;
			Image irradianceMap;
			u32 irradianceMapHandle = ~0u;
			Image radianceMap;
			u32 radianceMapHandle = ~0u;

			Skybox() = default;
			~Skybox() {
				if(heap) {
					heap->freeImageHandle(environmentMapHandle);
					heap->freeImageHandle(irradianceMapHandle);
					heap->freeImageHandle(radianceMapHandle);
				}
			}

			Skybox(Skybox&& src) {
				memcpy(this, &src, sizeof(Skybox));
				memset(&src, 0, sizeof(Skybox));
			}

			Skybox& operator=(Skybox&& src) {
				this->~Skybox();
				new (this) Skybox(std::move(src)); return *this;
			};
		};

		struct PushConstants {
			void* oitBuffer;
			void* vertexBuffer;
			void* materialBuffer;
			void* poissonDiskBuffer;
			glm::mat4 cameraTransform;
			glm::mat4x3 modelTransform;
			glm::vec4 lightColor;
			glm::vec3 camPos;
			glm::vec3 lightAngle;
			f32 orthoSize;
			u32 frameBufferWidth;
			ImageHandle irradianceMap;
			ImageHandle radianceMap;
			ImageHandle brdfIntegralTex;
			ImageHandle shadowMapTex;
		};

		struct BloomMip {
			u32 storageHandle;
			u32 sampledHandle;
			glm::uvec2 bounds;
		};

		struct {
			VkCommandPool cmdPool;
			VkCommandBuffer cmdBuffer;
			VkSemaphore acquireSem;
			Fence fence;
			std::vector<std::any> deletionQueue;
		} m_perFrameData[m_framesInFlight];

		Window m_window;

		// vulkan
		u8 m_frameIndex = 0;
		b8 m_swapchainDirty = false;

		VulkanContext m_ctx;
		DescriptorHeap m_heap;

		VkSurfaceKHR m_surface = {};
		VkSurfaceFormatKHR m_surfaceFormat;
		VkPresentModeKHR m_nonVsyncPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		VkSwapchainKHR m_swapchain = {};
		std::vector<VkImage> m_swapchainImages;
		std::vector<u32> m_swapchainHandles;
		std::vector<VkSemaphore> m_swapchainSems;
		std::vector<BloomMip> m_bloomMips;

		
		VkCommandPool m_transferPoolModelThread = {};
		VkCommandBuffer m_transferCmdModelThread = {};
		VkCommandPool m_transferPoolSkyboxThread = {};
		VkCommandBuffer m_transferCmdSkyboxThread = {};

		VkCommandPool m_computePoolModelThread = {};
		VkCommandBuffer m_computeCmdModelThread = {};
		VkCommandPool m_computePoolSkyboxThread = {};
		VkCommandBuffer m_computeCmdSkyboxThread = {};

		Pipeline m_prepassPipeline;
		Pipeline m_shadowPipeline;
		Pipeline m_opaquePipeline;
		Pipeline m_blendPipeline;
		Pipeline m_skyboxPipeline;
		Pipeline m_mipPipeline;
		Pipeline m_srgbMipPipeline;
		Pipeline m_cubePipeline;
		Pipeline m_cubeMipPipeline;
		Pipeline m_irradiancePipeline;
		Pipeline m_radiancePipeline;
		Pipeline m_transparencyCompositePipeline;
		Pipeline m_postprocessingPipeline;
		Pipeline m_uiCompositePipeline;
		Pipeline m_bloomDownsamplePipeline;
		Pipeline m_bloomUpsamplePipeline;
		Pipeline m_fxaaPipeline;
		Pipeline m_blitPipeline;

		Semaphore m_renderSem;
		Semaphore m_modelSem;
		Semaphore m_skyboxSem;

		Buffer m_poissonDiskBuffer;
		Buffer m_oitBuffer;
		Image m_colorTarget;
		VkImageView m_colorTargetView;
		u32 m_colorTargetHandleStorage;
		u32 m_colorTargetHandleSampled;
		Image m_depthTarget;
		VkImageView m_depthTargetView;
		Image m_uiTarget;
		VkImageView m_uiTargetView;
		u32 m_uiTargetHandle;
		Image m_bloomTarget;
		Model m_model;

		Skybox m_skybox;
		Image m_brdfIntegralTex;
		u32 m_brdfIntegralTexHandle;
		Image m_shadowMap;
		VkImageView m_shadowMapView;
		u32 m_shadowMapHandle;

		u32 m_genericSamplerHandle;
		u32 m_shadowSamplerHandle;

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

		void createSwapchain();
		void destroySwapchain();
		void recreateSwapchain();

		Model createModel(std::filesystem::path path);

		Skybox createSkybox(std::filesystem::path path);

		void handleInput(f32 deltaTime);
		void render(f32 thisFrame);
};

#endif
