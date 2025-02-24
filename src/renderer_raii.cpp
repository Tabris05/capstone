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
		m_maxSampledImageDescriptors = props.limits.maxPerStageDescriptorSampledImages;
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
							.maintenance5 = true,
							.pushDescriptor = true
						}),
						.synchronization2 = true,
						.dynamicRendering = true,
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
			.enabledExtensionCount = 1,
			.ppEnabledExtensionNames = ptr<const char*>({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME }),
			.pEnabledFeatures = ptr(VkPhysicalDeviceFeatures{ 
				.multiDrawIndirect = true,
				.drawIndirectFirstInstance = true,
				.samplerAnisotropy = true
			})
		}), nullptr, &m_device);

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
		vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);
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

	// transfer pipeline layouts
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
	}

	// transfer pipelines
	{
		m_srgbPipeline = createComputePipeline(m_oneImagePipelineLayout, "shaders/srgb.comp.spv");
		m_mipPipeline = createComputePipeline(m_twoImagePipelineLayout, "shaders/mip.comp.spv");
		m_cubePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, "shaders/cube.comp.spv");
	}

	// skybox VkSampler
	{
		vkCreateSampler(m_device, ptr(VkSamplerCreateInfo{
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.anisotropyEnable = true,
			.maxAnisotropy = 16,
		}), nullptr, &m_skyboxSampler);
	}

	// Model Pipeline
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

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_modelSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(PushConstants),
			})
		}), nullptr, &m_modelPipelineLayout);

		m_modelPipeline = createGraphicsPipeline(m_modelPipelineLayout, "shaders/model.vert.spv", "shaders/model.frag.spv");
	}

	// Skybox Pipeline
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.binding = 0,
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

		m_skyboxPipeline = createGraphicsPipeline(m_skyboxPipelineLayout, "shaders/skybox.vert.spv", "shaders/skybox.frag.spv");
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

	vkDestroyPipeline(m_device, m_cubePipeline, nullptr);
	vkDestroyPipeline(m_device, m_mipPipeline, nullptr);
	vkDestroyPipeline(m_device, m_srgbPipeline, nullptr);

	vkDestroyPipelineLayout(m_device, m_oneTexOneImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_oneTexOneImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_twoImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_twoImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_oneImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_oneImageSetLayout, nullptr);

	vkDestroyPipeline(m_device, m_skyboxPipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_skyboxPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_skyboxSetLayout, nullptr);

	vkDestroyPipeline(m_device, m_modelPipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_modelPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_modelSetLayout, nullptr);

	vkDestroySampler(m_device, m_skyboxSampler, nullptr);
	destroySkybox(m_skybox);
	destroyModel(m_model);

	destroyImage(m_colorTarget);
	destroyImage(m_depthTarget);
	
	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroyDevice(m_device, nullptr);

	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyInstance(m_instance, nullptr);

	NFD_Quit();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}