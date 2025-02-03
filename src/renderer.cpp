#include "renderer.h"
#include <tbrs/vk_util.hpp>
#include <ranges>
#include <fstream>

Renderer::Renderer() {
	// glfw
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
		vkCreateDevice(m_physicalDevice, ptr(VkDeviceCreateInfo{
			.pNext = ptr(VkPhysicalDeviceVulkan12Features{
				.pNext = ptr(VkPhysicalDeviceVulkan13Features{
					.pNext = ptr(VkPhysicalDeviceVulkan14Features{ .maintenance5 = true }),
					.synchronization2 = true,
					.dynamicRendering = true,
				}),
				.descriptorBindingVariableDescriptorCount = true,
				.scalarBlockLayout = true,
				.bufferDeviceAddress = true
			}),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = ptr(VkDeviceQueueCreateInfo{
				.queueFamilyIndex = m_graphicsQueueFamily,
				.queueCount = 1,
				.pQueuePriorities = ptr(1.0f)
			}),
			.enabledExtensionCount = 1,
			.ppEnabledExtensionNames = ptr<const char*>(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
		}), nullptr, &m_device);

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
	}

	// VkSurface and VkSwapchain
	{
		glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, ptr(1u), &m_surfaceFormat);

		m_swapchain = nullptr;
		createSwapchain();
	}

	// VkPipelineLayout and VkPipeline
	{
		std::vector<u32> vsSrc = getShaderSource("shaders/tri.vert.spv");
		std::vector<u32> fsSrc = getShaderSource("shaders/tri.frag.spv");

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{}), nullptr, &m_pipelineLayout);
		vkCreateGraphicsPipelines(m_device, nullptr, 1, ptr(VkGraphicsPipelineCreateInfo{
			.pNext = ptr(VkPipelineRenderingCreateInfo{
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &m_colorFormat
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
			.pRasterizationState = ptr(VkPipelineRasterizationStateCreateInfo{ .cullMode = VK_CULL_MODE_BACK_BIT, .lineWidth = 1.0f }),
			.pMultisampleState = ptr(VkPipelineMultisampleStateCreateInfo{ .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT }),
			.pDepthStencilState = ptr(VkPipelineDepthStencilStateCreateInfo{}),
			.pColorBlendState = ptr(VkPipelineColorBlendStateCreateInfo{ .attachmentCount = 1, .pAttachments = ptr(VkPipelineColorBlendAttachmentState{ .colorWriteMask = colorComponentAll() })}),
			.pDynamicState = ptr(VkPipelineDynamicStateCreateInfo{ .dynamicStateCount = 2, .pDynamicStates = ptr({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }) }),
			.layout = m_pipelineLayout
		}), nullptr, &m_pipeline);
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

	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	vkDestroyImageView(m_device, m_colorTarget.view, nullptr);
	vkDestroyImage(m_device, m_colorTarget.image, nullptr);
	vkFreeMemory(m_device, m_colorTarget.memory, nullptr);

	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroyDevice(m_device, nullptr);

	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyInstance(m_instance, nullptr);

	glfwDestroyWindow(m_window);
	glfwTerminate();
}

void Renderer::run() {
	while(!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();

		auto frameData = m_perFrameData[m_frameIndex];
		vkWaitForFences(m_device, 1, &frameData.fence, true, std::numeric_limits<u64>::max());

		u32 imageIndex;
		vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<u64>::max(), frameData.acquireSem, nullptr, &imageIndex);

		vkResetFences(m_device, 1, &frameData.fence);
		vkResetCommandPool(m_device, frameData.cmdPool, 0);
		
		vkBeginCommandBuffer(frameData.cmdBuffer, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.image = m_colorTarget.image,
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = ptr(VkRenderingAttachmentInfo{
				.imageView = m_colorTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f, 0.0f, 0.0f, 1.0f }
			})
		}));

		vkCmdSetViewport(frameData.cmdBuffer, 0, 1, ptr(VkViewport{ 0.0f, 0.0f, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f }));
		vkCmdSetScissor(frameData.cmdBuffer, 0, 1, ptr(VkRect2D{ { 0, 0 }, { static_cast<u32>(m_width), static_cast<u32>(m_height) } }));
		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdDraw(frameData.cmdBuffer, 3, 1, 0, 0);

		vkCmdEndRendering(frameData.cmdBuffer);

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					.srcAccessMask = VK_ACCESS_2_NONE,
					.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image = m_swapchainImages[imageIndex],
					.subresourceRange = colorSubresourceRange()
				}
			})
		}));

		vkCmdBlitImage2(frameData.cmdBuffer, ptr(VkBlitImageInfo2{
			.srcImage = m_colorTarget.image,
			.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.dstImage = m_swapchainImages[imageIndex],
			.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.regionCount = 1,
			.pRegions = ptr(VkImageBlit2{
				.srcSubresource = colorSubresourceLayers(),
				.srcOffsets = { { 0, 0, 0 }, { m_width, m_height, 1 } },
				.dstSubresource = colorSubresourceLayers(),
				.dstOffsets = { { 0, 0, 0 }, { m_width, m_height, 1 } }
			}),
			.filter = VK_FILTER_NEAREST
		}));

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
				.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				.image = m_swapchainImages[imageIndex],
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkEndCommandBuffer(frameData.cmdBuffer);

		vkQueueSubmit(m_graphicsQueue, 1, ptr(VkSubmitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &frameData.acquireSem,
			.pWaitDstStageMask = ptr<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TRANSFER_BIT),
			.commandBufferCount = 1,
			.pCommandBuffers = &frameData.cmdBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &frameData.presentSem
		}), frameData.fence);

		vkQueuePresentKHR(m_graphicsQueue, ptr(VkPresentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &frameData.presentSem,
			.swapchainCount = 1,
			.pSwapchains = &m_swapchain,
			.pImageIndices = &imageIndex
		}));

		m_frameIndex = (m_frameIndex + 1) % m_framesInFlight;
	}
}

void Renderer::onResize() {

}

u32 Renderer::getQueue(VkQueueFlags include, VkQueueFlags exclude) {
	u32 size = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &size, nullptr);

	std::vector<VkQueueFamilyProperties> queueProperties(size);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &size, queueProperties.data());

	for(auto [idx, queueFamily] : std::views::enumerate(queueProperties)) {
		if((queueFamily.queueFlags & include) && !(queueFamily.queueFlags & exclude)) {
			return idx;
		}
	}
}

u32 Renderer::getMemoryIndex(VkMemoryPropertyFlags flags, u32 mask) {
	for(u32 idx = 0; idx < m_memProps.memoryTypeCount; idx++) {
		if(((1 << idx) & mask) && (m_memProps.memoryTypes[idx].propertyFlags & flags) == flags) {
			return idx;
		}
	}
}

std::vector<u32> Renderer::getShaderSource(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	std::vector<u32> ret(file.tellg() / sizeof(u32));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(ret.data()), ret.size() * sizeof(u32));
	return ret;
}

void Renderer::createSwapchain() {
	VkSwapchainKHR oldSwapchain = m_swapchain;

	vkCreateSwapchainKHR(m_device, ptr(VkSwapchainCreateInfoKHR{
		.surface = m_surface,
		.minImageCount = 3,
		.imageFormat = m_surfaceFormat.format,
		.imageColorSpace = m_surfaceFormat.colorSpace,
		.imageExtent = { static_cast<u32>(m_width), static_cast<u32>(m_height) },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = ptr(m_graphicsQueueFamily),
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = true,
		.oldSwapchain = oldSwapchain
	}), nullptr, &m_swapchain);
	vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);

	u32 numSwapchainImages;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &numSwapchainImages, nullptr);

	m_swapchainImages.resize(numSwapchainImages);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &numSwapchainImages, m_swapchainImages.data());

	vkCreateImage(m_device, ptr(VkImageCreateInfo{
		.imageType = VK_IMAGE_TYPE_2D,
		.format = m_colorFormat,
		.extent = { static_cast<u32>(m_width), static_cast<u32>(m_height), 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &m_graphicsQueueFamily
	}), nullptr, &m_colorTarget.image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(m_device, m_colorTarget.image, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits)
	}), nullptr, &m_colorTarget.memory);
	vkBindImageMemory(m_device, m_colorTarget.image, m_colorTarget.memory, 0);

	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.image = m_colorTarget.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = m_colorFormat,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &m_colorTarget.view);
}