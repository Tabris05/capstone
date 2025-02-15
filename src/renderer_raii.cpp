#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <nfd/nfd.h>
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

	// VkPhysicalDevice and VkPhysicalDeviceMemoryProperties
	{
		vkEnumeratePhysicalDevices(m_instance, ptr(1u), &m_physicalDevice);
		vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProps);
	}

	// VkDevice and VkQueues
	{
		m_graphicsQueueFamily = getQueue(VK_QUEUE_GRAPHICS_BIT);
		m_computeQueueFamily = getQueue(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
		m_transferQueueFamily = getQueue(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
		vkCreateDevice(m_physicalDevice, ptr(VkDeviceCreateInfo{
			.pNext = ptr(VkPhysicalDeviceVulkan12Features{
				.pNext = ptr(VkPhysicalDeviceVulkan13Features{
					.pNext = ptr(VkPhysicalDeviceVulkan14Features{
						.maintenance5 = true,
						.pushDescriptor = true
					}),
					.synchronization2 = true,
					.dynamicRendering = true,
				}),
				.descriptorBindingVariableDescriptorCount = true,
				.scalarBlockLayout = true,
				.bufferDeviceAddress = true
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
			.ppEnabledExtensionNames = ptr<const char*>(VK_KHR_SWAPCHAIN_EXTENSION_NAME),
			.pEnabledFeatures = ptr(VkPhysicalDeviceFeatures{ 
				.multiDrawIndirect = true,
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

	// VkPipelineLayout and VkPipeline
	{
		std::vector<u32> vsSrc = getShaderSource("shaders/model.vert.spv");
		std::vector<u32> fsSrc = getShaderSource("shaders/model.frag.spv");

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.offset = 0,
				.size = sizeof(PushConstants)
			})
		}), nullptr, &m_modelPipelineLayout);
		vkCreateGraphicsPipelines(m_device, nullptr, 1, ptr(VkGraphicsPipelineCreateInfo{
			.pNext = ptr(VkPipelineRenderingCreateInfo{
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &m_colorFormat,
				.depthAttachmentFormat = m_depthFormat
			}),
			.stageCount = 2,
			.pStages = ptr({
				VkPipelineShaderStageCreateInfo{
					.pNext = ptr(VkShaderModuleCreateInfo{
						.codeSize = vsSrc.size() * sizeof(u32),
						.pCode = vsSrc.data()
					}),
					.stage = VK_SHADER_STAGE_VERTEX_BIT,
					.pName = "main"
				},
				VkPipelineShaderStageCreateInfo{
					.pNext = ptr(VkShaderModuleCreateInfo{
						.codeSize = fsSrc.size() * sizeof(u32),
						.pCode = fsSrc.data()
					}),
					.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
					.pName = "main"
				},
			}),
			.pVertexInputState = ptr(VkPipelineVertexInputStateCreateInfo{}),
			.pInputAssemblyState = ptr(VkPipelineInputAssemblyStateCreateInfo{ .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST }),
			.pViewportState = ptr(VkPipelineViewportStateCreateInfo{ .viewportCount = 1, .scissorCount = 1 }),
			.pRasterizationState = ptr(VkPipelineRasterizationStateCreateInfo{ .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f }),
			.pMultisampleState = ptr(VkPipelineMultisampleStateCreateInfo{ .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT }),
			.pDepthStencilState = ptr(VkPipelineDepthStencilStateCreateInfo{ .depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_GREATER }),
			.pColorBlendState = ptr(VkPipelineColorBlendStateCreateInfo{ .attachmentCount = 1, .pAttachments = ptr(VkPipelineColorBlendAttachmentState{ .colorWriteMask = colorComponentAll() })}),
			.pDynamicState = ptr(VkPipelineDynamicStateCreateInfo{ .dynamicStateCount = 2, .pDynamicStates = ptr({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }) }),
			.layout = m_modelPipelineLayout
		}), nullptr, &m_modelPipeline);
	}

	// Load Model
	{
		nfdu8char_t* outPath;
		nfdwindowhandle_t parentWindow;
		NFD_GetNativeWindowFromGLFWWindow(m_window, &parentWindow);

		nfdu8filteritem_t filters[2] = { { "glTF Binary", "glb" }, { "glTF Seperate", "gltf" } };
		nfdopendialogu8args_t args = { 0 };
		nfdresult_t result = NFD_OpenDialogU8_With(&outPath, ptr(nfdopendialogu8args_t{
			.filterList = ptr({ nfdu8filteritem_t{ "glTF Binary", "glb" }, nfdu8filteritem_t{ "glTF Seperate", "gltf" } }),
			.filterCount = 2,
			.parentWindow = parentWindow
			
		}));

		if(result == NFD_OKAY) {
			createModel(outPath);
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

	vkDestroyPipeline(m_device, m_modelPipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_modelPipelineLayout, nullptr);

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