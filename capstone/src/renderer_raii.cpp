#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <nfd/nfd_glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <random>
#include <numbers>
#include <fstream>
#include <shared/vertex.h>

//shaders
#include <shaders/blend_frag.h>
#include <shaders/blit_comp.h>
#include <shaders/bloomdownsample_comp.h>
#include <shaders/bloomupsample_comp.h>
#include <shaders/brdfintegral_comp.h>
#include <shaders/cube_comp.h>
#include <shaders/cubemip_comp.h>
#include <shaders/fxaa_comp.h>
#include <shaders/irradiance_comp.h>
#include <shaders/mip_comp.h>
#include <shaders/model_vert.h>
#include <shaders/opaque_frag.h>
#include <shaders/postprocess_comp.h>
#include <shaders/prepass_vert.h>
#include <shaders/radiance_comp.h>
#include <shaders/shadow_vert.h>
#include <shaders/skybox_frag.h>
#include <shaders/skybox_vert.h>
#include <shaders/srgbmip_comp.h>
#include <shaders/transparencycomposite_comp.h>
#include <shaders/uicomposite_comp.h>

#include <resources/roboto_regular.h>

Renderer::Renderer(const char* path) :
	m_heap(m_ctx.device()),
	m_poissonDiskBuffer(m_ctx, m_poissonDiskBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	m_brdfIntegralTex(m_ctx, m_brdfIntegralLUTSize, m_brdfIntegralLUTSize, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
	m_brdfIntegralTexHandle(m_heap.allocImageHandle(ptr(m_brdfIntegralTex.viewCI()), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)),
	m_shadowMap(m_ctx, m_shadowMapSize, m_shadowMapSize, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
	m_shadowMapHandle(m_heap.allocImageHandle(ptr(m_shadowMap.viewCI()), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)) {

	// glfw
	{
		glfwSetWindowUserPointer(m_window.window(), this);
		glfwSetFramebufferSizeCallback(m_window.window(), [](GLFWwindow* window, i32 width, i32 height) {
			reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window))->onResize();
		});
		glfwSetScrollCallback(m_window.window(), [](GLFWwindow* window, f64 xOffset, f64 yOffset) {
			reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window))->onScroll(yOffset);
		});
	}

	// per-frame data (vk::CommandPool, vk::CommandBuffer, vk::Semaphores, vk::Fence)
	{
		for(u8 i = 0; i < m_framesInFlight; i++) {
			vkCreateCommandPool(m_ctx.device(), ptr(VkCommandPoolCreateInfo{
				.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
				.queueFamilyIndex = m_ctx.queueFamilies()[0]
			}), nullptr, &m_perFrameData[i].cmdPool);
			vkAllocateCommandBuffers(m_ctx.device(), ptr(VkCommandBufferAllocateInfo{
				.commandPool = m_perFrameData[i].cmdPool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1
			}), &m_perFrameData[i].cmdBuffer);
			vkCreateSemaphore(m_ctx.device(), ptr(VkSemaphoreCreateInfo{}), nullptr, &m_perFrameData[i].acquireSem);
		}

		m_renderSem = Semaphore(m_ctx.device());
	}

	// VkSurface and VkSwapchain
	{
		glfwCreateWindowSurface(m_ctx.instance(), m_window.window(), nullptr, &m_surface);

		u32 count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physicalDevice(), m_surface, &count, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physicalDevice(), m_surface, &count, formats.data());
		m_surfaceFormat = formats.front();
		for(auto format : formats) {
			if(!vkuFormatIsSRGB(format.format)) {
				m_surfaceFormat = format;
				break;
			}
		}

		count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx.physicalDevice(), m_surface, &count, nullptr);
		std::vector<VkPresentModeKHR> modes(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx.physicalDevice(), m_surface, &count, modes.data());
		
		for(auto mode : modes) {
			if(mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				m_nonVsyncPresentMode == VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			}
		}

		createSwapchain();
	}

	// transfer objects
	{
		vkCreateCommandPool(m_ctx.device(), ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_ctx.queueFamilies()[2]
		}), nullptr, &m_transferPoolModelThread);
		vkAllocateCommandBuffers(m_ctx.device(), ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_transferPoolModelThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_transferCmdModelThread);

		vkCreateCommandPool(m_ctx.device(), ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_ctx.queueFamilies()[2]
		}), nullptr, &m_transferPoolSkyboxThread);
		vkAllocateCommandBuffers(m_ctx.device(), ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_transferPoolSkyboxThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_transferCmdSkyboxThread);

		vkCreateCommandPool(m_ctx.device(), ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_ctx.queueFamilies()[1]
		}), nullptr, &m_computePoolModelThread);
		vkAllocateCommandBuffers(m_ctx.device(), ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_computePoolModelThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_computeCmdModelThread);

		vkCreateCommandPool(m_ctx.device(), ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_ctx.queueFamilies()[1]
			}), nullptr, &m_computePoolSkyboxThread);
		vkAllocateCommandBuffers(m_ctx.device(), ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_computePoolSkyboxThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_computeCmdSkyboxThread);

		m_modelSem = Semaphore(m_ctx.device());
		m_skyboxSem = Semaphore(m_ctx.device());
	}

	// compute pipelines
	{
		m_mipPipeline = createComputePipeline(mip_comp);
		m_srgbMipPipeline = createComputePipeline(srgbmip_comp);
		m_cubePipeline = createComputePipeline(cube_comp);
		m_cubeMipPipeline = createComputePipeline(cubemip_comp);
		m_irradiancePipeline = createComputePipeline(irradiance_comp);
		m_radiancePipeline = createComputePipeline(radiance_comp);
		m_brdfIntegralPipeline = createComputePipeline(brdfintegral_comp);
		m_transparencyCompositePipeline = createComputePipeline(transparencycomposite_comp);
		m_postprocessingPipeline = createComputePipeline(postprocess_comp);
		m_uiCompositePipeline = createComputePipeline(uicomposite_comp);
		m_bloomDownsamplePipeline = createComputePipeline(bloomdownsample_comp);
		m_bloomUpsamplePipeline = createComputePipeline(bloomupsample_comp);
		m_fxaaPipeline = createComputePipeline(fxaa_comp);
		m_blitPipeline = createComputePipeline(blit_comp);
	}

	// global samplers
	{
		m_genericSamplerHandle = m_heap.allocSamplerHandle(ptr(VkSamplerCreateInfo{
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.anisotropyEnable = true,
			.maxAnisotropy = 16,
			.maxLod = VK_LOD_CLAMP_NONE
		}));

		m_shadowSamplerHandle = m_heap.allocSamplerHandle(ptr(VkSamplerCreateInfo{
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.compareEnable = true,
			.compareOp = VK_COMPARE_OP_GREATER
		}));
	}

	// generate brdf integral tex
	{
		u32 brdfImageHandle = m_heap.allocImageHandle(ptr(m_brdfIntegralTex.viewCI()), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		vkBeginCommandBuffer(m_computeCmdModelThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
		
		vkCmdInitializeColorImage(m_computeCmdModelThread, m_brdfIntegralTex.image());
		m_heap.bind(m_computeCmdModelThread);

		vkCmdBindPipeline(m_computeCmdModelThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfIntegralPipeline);
		vkCmdPushConstants(m_computeCmdModelThread, m_heap.layout(), VK_SHADER_STAGE_ALL, 0, sizeof(u32), &brdfImageHandle);

		vkCmdDispatch(m_computeCmdModelThread, (m_brdfIntegralLUTSize + 7) / 8, (m_brdfIntegralLUTSize + 7) / 8, 1);

		vkEndCommandBuffer(m_computeCmdModelThread);

		vkQueueSubmit2(m_ctx.computeQueue(), 1, ptr(VkSubmitInfo2{
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_computeCmdModelThread })
		}), nullptr);

		vkQueueWaitIdle(m_ctx.computeQueue());
		vkResetCommandPool(m_ctx.device(), m_computePoolModelThread, 0);
		m_heap.freeImageHandle(brdfImageHandle);
	}

	// upload blue noise
	{
		std::vector<glm::vec2> samples;
		samples.reserve(m_poissonDiskBufferSize / sizeof(glm::vec2));

		std::default_random_engine generator;
		std::uniform_real_distribution<f32> distribution(-0.5f, 0.5f);

		for(i32 v = m_poissonDiskFilterSize - 1; v >= 0; v--) {
			for(i32 u = 0; u < m_poissonDiskFilterSize; u++) {
				for(i32 i = 0; i < m_poissonDiskWindowSize * m_poissonDiskWindowSize; i++) {
					f32 x = (u + 0.5f + distribution(generator)) / m_poissonDiskFilterSize;
					f32 y = (v + 0.5f + distribution(generator)) / m_poissonDiskFilterSize;
					
					glm::vec2 sample(std::sqrtf(y) * std::cosf(2.0f * std::numbers::pi_v<f32> * x), std::sqrtf(y) * std::sinf(2.0f * std::numbers::pi_v<f32> * x));
					samples.push_back(sample);
				}
			}
		}

		Buffer poissonDiskStagingBuffer(m_ctx, m_poissonDiskBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		memcpy(poissonDiskStagingBuffer.map<void>(), samples.data(), m_poissonDiskBufferSize);

		vkBeginCommandBuffer(m_transferCmdModelThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
		vkCmdCopyBuffer(m_transferCmdModelThread, poissonDiskStagingBuffer.buffer(), m_poissonDiskBuffer.buffer(), 1, ptr(VkBufferCopy{ .size = m_poissonDiskBufferSize }));
		vkEndCommandBuffer(m_transferCmdModelThread);

		vkQueueSubmit2(m_ctx.transferQueue(), 1, ptr(VkSubmitInfo2{
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_transferCmdModelThread })
		}), nullptr);

		vkQueueWaitIdle(m_ctx.transferQueue());
		vkResetCommandPool(m_ctx.device(), m_transferPoolModelThread, 0);
	}

	// shadow map image view
	{
		vkCreateImageView(m_ctx.device(), ptr(m_shadowMap.viewCI()), nullptr, &m_shadowMapView);
	}

	// Color Pass Pipelines
	{
		m_opaquePipeline = createGraphicsPipeline(model_vert, opaque_frag, VK_CULL_MODE_BACK_BIT, VK_COMPARE_OP_EQUAL, false, true);
		m_blendPipeline = createGraphicsPipeline(model_vert, blend_frag, VK_CULL_MODE_NONE, VK_COMPARE_OP_GREATER, false, false);
		m_skyboxPipeline = createGraphicsPipeline(skybox_vert, skybox_frag, VK_CULL_MODE_NONE, VK_COMPARE_OP_EQUAL, false, true);
	}

	// Depth Only Pipelines
	{
		m_prepassPipeline = createGraphicsPipeline(prepass_vert, {}, VK_CULL_MODE_BACK_BIT, VK_COMPARE_OP_GREATER, true, false);
		m_shadowPipeline = createGraphicsPipeline(shadow_vert, {}, VK_CULL_MODE_BACK_BIT, VK_COMPARE_OP_GREATER, true, false);
	}

	// ImGui
	{
		ImGui::CreateContext();
		ImGui_ImplGlfw_InitForVulkan(m_window.window(), true);
		ImGui_ImplVulkan_Init(ptr(ImGui_ImplVulkan_InitInfo{
			.ApiVersion = VK_API_VERSION_1_4,
			.Instance = m_ctx.instance(),
			.PhysicalDevice = m_ctx.physicalDevice(),
			.Device = m_ctx.device(),
			.QueueFamily = m_ctx.queueFamilies()[0],
			.Queue = m_ctx.graphicsQueue(),
			.MinImageCount = 3,
			.ImageCount = 3,
			.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE,
			.UseDynamicRendering = true,
			.PipelineRenderingCreateInfo{
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &m_uiFormat
			}
		}));
	}

	// ImGui Styling
	{
		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();

		ImFontConfig cfg;
		cfg.FontDataOwnedByAtlas = false;
		io.Fonts->AddFontFromMemoryTTF(roboto_regular, sizeof(roboto_regular), 18, &cfg);
		m_largeFont = io.Fonts->AddFontFromMemoryTTF(roboto_regular, sizeof(roboto_regular), 36, &cfg);

		style.WindowMinSize = ImVec2(160, 20);
		style.FramePadding = ImVec2(4, 2);
		style.ItemSpacing = ImVec2(6, 2);
		style.ItemInnerSpacing = ImVec2(6, 4);
		style.Alpha = 0.95f;
		style.WindowRounding = 4.0f;
		style.FrameRounding = 2.0f;
		style.IndentSpacing = 6.0f;
		style.ItemInnerSpacing = ImVec2(2, 4);
		style.ColumnsMinSpacing = 50.0f;
		style.GrabMinSize = 14.0f;
		style.GrabRounding = 16.0f;
		style.ScrollbarSize = 12.0f;
		style.ScrollbarRounding = 16.0f;

		style.Colors[ImGuiCol_Text] = ImVec4(0.86f, 0.93f, 0.89f, 0.78f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.47f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.92f, 0.18f, 0.29f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.77f, 0.83f, 0.04f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.92f, 0.18f, 0.29f, 0.43f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.9f);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.73f);
	}

	if(path) {
		m_loadingScene = true;
		m_loadingSceneFuture = std::async(std::launch::async, [this, path] {
			std::ifstream file(path, std::ios::binary);

			auto readVariable = [&file](auto& var) {
				file.read(reinterpret_cast<char*>(&var), sizeof(var));
			};

			readVariable(m_scroll);
			readVariable(m_position);
			readVariable(m_rotation);
			readVariable(m_vsync);
			readVariable(m_camIdx);
			readVariable(m_colorIdx);
			readVariable(m_AAIdx);
			readVariable(m_fov);
			readVariable(m_sensitivity);
			readVariable(m_scrollSensitivity);
			readVariable(m_lightPitch);
			readVariable(m_lightYaw);
			readVariable(m_modelPitch);
			readVariable(m_modelYaw);
			readVariable(m_modelRoll);
			readVariable(m_modelScale);
			readVariable(m_lightAngle);
			readVariable(m_lightColor);

			u64 size = 0;
			readVariable(size);
			m_modelPath.resize(size);
			file.read(m_modelPath.data(), size);

			readVariable(size);
			m_skyboxPath.resize(size);
			file.read(m_skyboxPath.data(), size);
		});
	}
}

Renderer::~Renderer() {
	vkDeviceWaitIdle(m_ctx.device());

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	for(u8 i = 0; i < m_framesInFlight; i++) {
		vkDestroyCommandPool(m_ctx.device(), m_perFrameData[i].cmdPool, nullptr);
		vkDestroySemaphore(m_ctx.device(), m_perFrameData[i].acquireSem, nullptr);
	}

	vkDestroyCommandPool(m_ctx.device(), m_transferPoolSkyboxThread, nullptr);
	vkDestroyCommandPool(m_ctx.device(), m_computePoolSkyboxThread, nullptr);

	vkDestroyCommandPool(m_ctx.device(), m_transferPoolModelThread, nullptr);
	vkDestroyCommandPool(m_ctx.device(), m_computePoolModelThread, nullptr);

	vkDestroyPipeline(m_ctx.device(), m_blitPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_fxaaPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_bloomUpsamplePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_bloomDownsamplePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_uiCompositePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_postprocessingPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_transparencyCompositePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_brdfIntegralPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_radiancePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_irradiancePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_cubeMipPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_cubePipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_mipPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_srgbMipPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_skyboxPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_shadowPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_prepassPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_blendPipeline, nullptr);
	vkDestroyPipeline(m_ctx.device(), m_opaquePipeline, nullptr);

	vkDestroyImageView(m_ctx.device(), m_shadowMapView, nullptr);
	m_heap.freeImageHandle(m_brdfIntegralTexHandle);
	m_heap.freeImageHandle(m_shadowMapHandle);
	m_heap.freeSamplerHandle(m_genericSamplerHandle);
	m_heap.freeSamplerHandle(m_shadowSamplerHandle);

	destroySwapchain();
	vkDestroySwapchainKHR(m_ctx.device(), m_swapchain, nullptr);
	vkDestroySurfaceKHR(m_ctx.instance(), m_surface, nullptr);
}