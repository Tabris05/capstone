#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <nfd/nfd_glfw3.h>
#include "../shared/vertex.h"

Renderer::Renderer() {
	// glfw and NFD
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
		m_width = mode->width * 3 / 4;
		m_height = mode->height * 3 / 4;

		m_window = glfwCreateWindow(m_width, m_height, "Capstone", nullptr, nullptr);

		glfwSetWindowUserPointer(m_window, this);
		glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, i32 width, i32 height) {
			reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window))->onResize();
		});

		NFD_Init();
		NFD_GetNativeWindowFromGLFWWindow(m_window, &m_nativeHandle);
	}

	// volk and VkInstance
	{
		volkInitialize();

		u32 glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		vkCreateInstance(ptr(VkInstanceCreateInfo{
			.pApplicationInfo = ptr(VkApplicationInfo{ .apiVersion = VK_API_VERSION_1_4 }),
			.enabledExtensionCount = glfwExtensionCount,
			.ppEnabledExtensionNames = glfwExtensions
		}), nullptr, &m_instance);

		volkLoadInstanceOnly(m_instance);
	}

	// VkPhysicalDevice, VkPhysicalDeviceMemoryProperties, and VkPhysicalDeviceProperties::limits::maxPerStageDescriptorSampledImages
	{
		vkEnumeratePhysicalDevices(m_instance, ptr(1u), &m_physicalDevice);
		vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProps);

		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
		m_maxSampledImageDescriptors = std::min(props.limits.maxPerStageDescriptorSampledImages, props.limits.maxPerStageDescriptorSamplers) - 3;
	}

	// VkDevice and VkQueues
	{
		m_graphicsQueueFamily = getQueue(VK_QUEUE_GRAPHICS_BIT);
		m_computeQueueFamily = getQueue(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
		m_transferQueueFamily = getQueue(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
		vkCreateDevice(m_physicalDevice, ptr(VkDeviceCreateInfo{
			.pNext = ptr(VkPhysicalDeviceVulkan11Features{
				.pNext = ptr(VkPhysicalDeviceVulkan12Features{
					.pNext = ptr(VkPhysicalDeviceVulkan13Features{
						.pNext = ptr(VkPhysicalDeviceVulkan14Features{
							.pNext = ptr(VkPhysicalDeviceRobustness2FeaturesEXT{
								.pNext = ptr(VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT{
									.pNext = ptr(VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR{ .shaderMaximalReconvergence = true }),
									.fragmentShaderPixelInterlock = true
								}),
								.nullDescriptor = true
							}),
							.maintenance5 = true,
							.pushDescriptor = true,
						}),
						.synchronization2 = true,
						.dynamicRendering = true
					}),
					.shaderSampledImageArrayNonUniformIndexing = true,
					.descriptorBindingVariableDescriptorCount = true,
					.runtimeDescriptorArray = true,
					.scalarBlockLayout = true,
					.bufferDeviceAddress = true
				}),
				.shaderDrawParameters = true
			}),
			.queueCreateInfoCount = 3,
			.pQueueCreateInfos = ptr({
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_graphicsQueueFamily,
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				},
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_computeQueueFamily,
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				},
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_transferQueueFamily,
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				}
			}),
			.enabledExtensionCount = 4,
			.ppEnabledExtensionNames = ptr<const char*>({
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
				VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,
				VK_KHR_SHADER_MAXIMAL_RECONVERGENCE_EXTENSION_NAME
			}),
			.pEnabledFeatures = ptr(VkPhysicalDeviceFeatures{ 
				.multiDrawIndirect = true,
				.drawIndirectFirstInstance = true,
				.samplerAnisotropy = true,
				.shaderInt64 = true
			})
		}), nullptr, &m_device);

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
		vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);
	}

	// per-frame data (vk::CommandPool, vk::CommandBuffer, vk::Semaphores, vk::Fence)
	{
		for(u8 i = 0; i < m_framesInFlight; i++) {
			vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
				.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
				.queueFamilyIndex = m_graphicsQueueFamily
			}), nullptr, &m_perFrameData[i].cmdPool);
			vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
				.commandPool = m_perFrameData[i].cmdPool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1
			}), &m_perFrameData[i].cmdBuffer);
			vkCreateSemaphore(m_device, ptr(VkSemaphoreCreateInfo{}), nullptr, &m_perFrameData[i].acquireSem);
			vkCreateSemaphore(m_device, ptr(VkSemaphoreCreateInfo{}), nullptr, &m_perFrameData[i].presentSem);
			vkCreateFence(m_device, ptr(VkFenceCreateInfo{ .flags = VK_FENCE_CREATE_SIGNALED_BIT }), nullptr, &m_perFrameData[i].fence);
		}
	}

	// VkSurface and VkSwapchain
	{
		glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, ptr(1u), &m_surfaceFormat);

		createSwapchain();
	}

	// transfer objects
	{
		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_transferQueueFamily
		}), nullptr, &m_transferPool);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_transferPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_transferCmd);

		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_computeQueueFamily
		}), nullptr, &m_computePool);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_computePool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_computeCmd);

		vkCreateSemaphore(m_device, ptr(VkSemaphoreCreateInfo{}), nullptr, &m_transferToComputeSem);
	}

	// compute pipeline layouts
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			})
		}), nullptr, &m_oneImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_oneImageSetLayout
		}), nullptr, &m_oneImagePipelineLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 2,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				}
			})
		}), nullptr, &m_twoImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_twoImageSetLayout
		}), nullptr, &m_twoImagePipelineLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 2,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				}
			})
		}), nullptr, &m_oneTexOneImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_oneTexOneImageSetLayout
		}), nullptr, &m_oneTexOneImagePipelineLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_twoImageSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = sizeof(PostProcessingPushConstants)
			})
		}), nullptr, &m_postprocessingPipelineLayout);
	}

	// compute pipelines
	{
		m_mipPipeline = createComputePipeline(m_twoImagePipelineLayout, "shaders/mip.comp.spv");
		m_srgbMipPipeline = createComputePipeline(m_twoImagePipelineLayout, "shaders/srgbmip.comp.spv");
		m_cubePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, "shaders/cube.comp.spv");
		m_cubeMipPipeline = createComputePipeline(m_twoImagePipelineLayout, "shaders/cubemip.comp.spv");
		m_irradiancePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, "shaders/irradiance.comp.spv");
		m_radiancePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, "shaders/radiance.comp.spv");
		m_brdfIntegralPipeline = createComputePipeline(m_oneImagePipelineLayout, "shaders/brdfintegral.comp.spv");
		m_postprocessingPipeline = createComputePipeline(m_postprocessingPipelineLayout, "shaders/postprocess.comp.spv");
	}

	// skybox VkSampler
	{
		vkCreateSampler(m_device, ptr(VkSamplerCreateInfo{
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.anisotropyEnable = true,
			.maxAnisotropy = 16,
			.maxLod = VK_LOD_CLAMP_NONE
		}), nullptr, &m_skyboxSampler);
	}

	// generate brdf integral tex
	{
		m_brdfIntegralTex = createImage(m_brdfIntegralLUTSize, m_brdfIntegralLUTSize, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		vkBeginCommandBuffer(m_computeCmd, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
		
		vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = m_brdfIntegralTex.image,
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkCmdBindPipeline(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfIntegralPipeline);

		vkCmdPushDescriptorSet(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = m_brdfIntegralTex.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}));

		vkCmdDispatch(m_computeCmd, (m_brdfIntegralLUTSize + 7) / 8, (m_brdfIntegralLUTSize + 7) / 8, 1);

		vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = m_brdfIntegralTex.image,
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkEndCommandBuffer(m_computeCmd);

		vkQueueSubmit2(m_computeQueue, 1, ptr(VkSubmitInfo2{
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_computeCmd })
		}), nullptr);

		vkQueueWaitIdle(m_computeQueue);
		vkResetCommandPool(m_device, m_computePool, 0);
	}

	// Opaque Pipeline
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.pNext = ptr(VkDescriptorSetLayoutBindingFlagsCreateInfo{
				.bindingCount = 1,
				.pBindingFlags = ptr<VkDescriptorBindingFlags>(VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
			}),
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = m_maxSampledImageDescriptors,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
			})
		}), nullptr, &m_modelSetLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 3,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				}
			})
		}), nullptr, &m_IBLSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 2,
			.pSetLayouts = ptr({ m_modelSetLayout, m_IBLSetLayout }),
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(PushConstants),
			})
		}), nullptr, &m_modelPipelineLayout);

		m_opaquePipeline = createGraphicsPipeline(m_modelPipelineLayout, "shaders/model.vert.spv", "shaders/opaque.frag.spv", VK_CULL_MODE_BACK_BIT, true, true);
		m_blendPipeline = createGraphicsPipeline(m_modelPipelineLayout, "shaders/model.vert.spv", "shaders/blend.frag.spv", VK_CULL_MODE_NONE, false, false);
	}

	// Skybox Pipeline
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
			})
		}), nullptr, &m_skyboxSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_skyboxSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.offset = 0,
				.size = sizeof(glm::mat4),
			})
		}), nullptr, &m_skyboxPipelineLayout);

		m_skyboxPipeline = createGraphicsPipeline(m_skyboxPipelineLayout, "shaders/skybox.vert.spv", "shaders/skybox.frag.spv", VK_CULL_MODE_NONE, false, true);
	}

	// Load Model
	{
		nfdu8char_t* outPath;

		nfdopendialogu8args_t args = { 0 };
		nfdresult_t result = NFD_OpenDialogU8_With(&outPath, ptr(nfdopendialogu8args_t{
			.filterList = ptr({ nfdu8filteritem_t{ "glTF Binary", "glb" }, nfdu8filteritem_t{ "glTF Seperate", "gltf" } }),
			.filterCount = 2,
			.parentWindow = m_nativeHandle
		}));

		if(result == NFD_OKAY) {
			createModel(outPath);
			NFD_FreePathU8(outPath);
		}
	}

	// Load Environment Map
	{
		nfdu8char_t* outPath;

		nfdopendialogu8args_t args = { 0 };
		nfdresult_t result = NFD_OpenDialogU8_With(&outPath, ptr(nfdopendialogu8args_t{
			.filterList = ptr(nfdu8filteritem_t{ "Environment Map", "hdr" }),
			.filterCount = 1,
			.parentWindow = m_nativeHandle
		}));

		if(result == NFD_OKAY) {
			createSkybox(outPath);
			NFD_FreePathU8(outPath);
		}
	}
}

Renderer::~Renderer() {
	vkDeviceWaitIdle(m_device);

	for(u8 i = 0; i < m_framesInFlight; i++) {
		vkDestroyCommandPool(m_device, m_perFrameData[i].cmdPool, nullptr);
		vkDestroySemaphore(m_device, m_perFrameData[i].acquireSem, nullptr);
		vkDestroySemaphore(m_device, m_perFrameData[i].presentSem, nullptr);
		vkDestroyFence(m_device, m_perFrameData[i].fence, nullptr);
	}

	vkDestroyCommandPool(m_device, m_transferPool, nullptr);
	vkDestroyCommandPool(m_device, m_computePool, nullptr);
	vkDestroySemaphore(m_device, m_transferToComputeSem, nullptr);

	vkDestroyPipeline(m_device, m_postprocessingPipeline, nullptr);
	vkDestroyPipeline(m_device, m_brdfIntegralPipeline, nullptr);
	vkDestroyPipeline(m_device, m_radiancePipeline, nullptr);
	vkDestroyPipeline(m_device, m_irradiancePipeline, nullptr);
	vkDestroyPipeline(m_device, m_cubeMipPipeline, nullptr);
	vkDestroyPipeline(m_device, m_cubePipeline, nullptr);
	vkDestroyPipeline(m_device, m_mipPipeline, nullptr);
	vkDestroyPipeline(m_device, m_srgbMipPipeline, nullptr);

	vkDestroyPipelineLayout(m_device, m_postprocessingPipelineLayout, nullptr);
	
	vkDestroyPipelineLayout(m_device, m_oneTexOneImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_oneTexOneImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_twoImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_twoImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_oneImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_oneImageSetLayout, nullptr);

	vkDestroyPipeline(m_device, m_skyboxPipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_skyboxPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_skyboxSetLayout, nullptr);

	vkDestroyPipeline(m_device, m_blendPipeline, nullptr);
	vkDestroyPipeline(m_device, m_opaquePipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_modelPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_modelSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_IBLSetLayout, nullptr);

	vkDestroySampler(m_device, m_skyboxSampler, nullptr);
	destroySkybox(m_skybox);
	destroyModel(m_model);

	destroyImage(m_brdfIntegralTex);
	destroyImage(m_colorTarget);
	destroyImage(m_depthTarget);
	destroyBuffer(m_oitBuffer);
	
	for(VkImageView view : m_swapchainImageViews) {
		vkDestroyImageView(m_device, view, nullptr);
	}

	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroyDevice(m_device, nullptr);

	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyInstance(m_instance, nullptr);

	NFD_Quit();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}