#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <ranges>
#include <fstream>
#include "../shared/vertex.h"

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
		m_transferQueueFamily = getQueue(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
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
			.queueCreateInfoCount = 2,
			.pQueueCreateInfos = ptr({
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_graphicsQueueFamily,
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
			.ppEnabledExtensionNames = ptr<const char*>(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
		}), nullptr, &m_device);

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);
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

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.offset = 0,
				.size = sizeof(PushConstants)
			})
		}), nullptr, &m_pipelineLayout);
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
			.layout = m_pipelineLayout
		}), nullptr, &m_pipeline);
	}

	// Vertex Buffer
	{
		Vertex vertices[] = {
			{ glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec3(0.0f, 0.0f, 0.0f) },
			{ glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec3(0.0f, 0.0f, 1.0f) },
			{ glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.0f, 1.0f, 0.0f) },
			{ glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec3(0.0f, 1.0f, 1.0f) },
			{ glm::vec3( 0.5f,  0.5f, -0.5f), glm::vec3(1.0f, 0.0f, 0.0f) },
			{ glm::vec3( 0.5f,  0.5f,  0.5f), glm::vec3(1.0f, 0.0f, 1.0f) },
			{ glm::vec3( 0.5f, -0.5f, -0.5f), glm::vec3(1.0f, 1.0f, 0.0f) },
			{ glm::vec3( 0.5f, -0.5f,  0.5f), glm::vec3(1.0f, 1.0f, 1.0f) },
		};

		u16 indices[] = { 0, 3, 1, 0, 2, 3, 5, 0, 1, 5, 4, 0, 4, 2, 0, 4, 6, 2, 5, 6, 4, 5, 7, 6, 6, 3, 2, 6, 7, 3, 1, 7, 5, 1, 3, 7 };

		Buffer stageVB = createBuffer(sizeof(vertices), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		Buffer stageIB = createBuffer(sizeof(indices), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		m_vertexBuffer = createBuffer(sizeof(vertices), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		m_indexBuffer = createBuffer(sizeof(indices), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		memcpy(stageVB.hostPtr, vertices, sizeof(vertices));
		memcpy(stageIB.hostPtr, indices, sizeof(indices));

		VkCommandPool stagingPool;
		VkCommandBuffer stagingCmd;
		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_transferQueueFamily
		}), nullptr, &stagingPool);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = stagingPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &stagingCmd);

		vkBeginCommandBuffer(stagingCmd, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
		vkCmdCopyBuffer(stagingCmd, stageVB.buffer, m_vertexBuffer.buffer, 1, ptr(VkBufferCopy{ .size = sizeof(vertices) }));
		vkCmdCopyBuffer(stagingCmd, stageIB.buffer, m_indexBuffer.buffer, 1, ptr(VkBufferCopy{ .size = sizeof(indices) }));
		vkEndCommandBuffer(stagingCmd);

		vkQueueSubmit(m_transferQueue, 1, ptr(VkSubmitInfo{
			.commandBufferCount = 1,
			.pCommandBuffers = &stagingCmd
		}), nullptr);

		vkQueueWaitIdle(m_transferQueue);
		
		destroyBuffer(stageVB);
		destroyBuffer(stageIB);

		vkDestroyCommandPool(m_device, stagingPool, nullptr);
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

	destroyBuffer(m_vertexBuffer);
	destroyBuffer(m_indexBuffer);

	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	destroyImage(m_colorTarget);
	destroyImage(m_depthTarget);
	
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

		glm::mat4 model = glm::rotate(glm::mat4(1.0f), static_cast<f32>(glfwGetTime()), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(m_position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 projection = glm::infinitePerspective(glm::radians(m_fov / 2.0f), static_cast<f32>(m_width) / static_cast<f32>(m_height), 0.1f);
		projection[2][2] = 0.0f;
		projection[3][2] = 0.1f;

		PushConstants pushConstants{ m_vertexBuffer.devicePtr, projection * view * model };

		auto frameData = m_perFrameData[m_frameIndex];
		vkWaitForFences(m_device, 1, &frameData.fence, true, std::numeric_limits<u64>::max());

		u32 imageIndex;
		VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<u64>::max(), frameData.acquireSem, nullptr, &imageIndex);
		if(result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapchain();
			continue;
		}

		vkResetFences(m_device, 1, &frameData.fence);
		vkResetCommandPool(m_device, frameData.cmdPool, 0);
		
		vkBeginCommandBuffer(frameData.cmdBuffer, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
					.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					.image = m_depthTarget.image,
					.subresourceRange = depthSubresourceRange()
				},
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
			}),
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_depthTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.clearValue = { 0.0f }
			})
		}));

		vkCmdSetViewport(frameData.cmdBuffer, 0, 1, ptr(VkViewport{ 0.0f, 0.0f, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f }));
		vkCmdSetScissor(frameData.cmdBuffer, 0, 1, ptr(VkRect2D{ { 0, 0 }, { static_cast<u32>(m_width), static_cast<u32>(m_height) } }));
		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindIndexBuffer(frameData.cmdBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdPushConstants(frameData.cmdBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);
		vkCmdDrawIndexed(frameData.cmdBuffer, 36, 1, 0, 0, 0);

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

		result = vkQueuePresentKHR(m_graphicsQueue, ptr(VkPresentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &frameData.presentSem,
			.swapchainCount = 1,
			.pSwapchains = &m_swapchain,
			.pImageIndices = &imageIndex
		}));

		if(result != VK_SUCCESS || m_swapchainDirty) {
			recreateSwapchain();
		}

		m_frameIndex = (m_frameIndex + 1) % m_framesInFlight;
	}
}

void Renderer::onResize() {
	m_swapchainDirty = true;
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

	m_colorTarget = createImage(m_width, m_height, m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	m_depthTarget = createImage(m_width, m_height, m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void Renderer::recreateSwapchain() {
	glfwGetFramebufferSize(m_window, &m_width, &m_height);
	while(m_width == 0 || m_height == 0) {
		glfwGetFramebufferSize(m_window, &m_width, &m_height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(m_device);
	destroyImage(m_colorTarget);
	destroyImage(m_depthTarget);

	createSwapchain();
	m_swapchainDirty = false;
}

Renderer::Image Renderer::createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage) {
	Image image;
	vkCreateImage(m_device, ptr(VkImageCreateInfo{
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &m_graphicsQueueFamily
	}), nullptr, &image.image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(m_device, image.image, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits)
	}), nullptr, &image.memory);
	vkBindImageMemory(m_device, image.image, image.memory, 0);

	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.image = image.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = (format < 124 || format > 130) ? colorSubresourceRange() : depthSubresourceRange()
	}), nullptr, &image.view);

	return image;
}

void Renderer::destroyImage(Image image) {
	vkDestroyImageView(m_device, image.view, nullptr);
	vkDestroyImage(m_device, image.image, nullptr);
	vkFreeMemory(m_device, image.memory, nullptr);
}

Renderer::Buffer Renderer::createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps) {
	Buffer buffer;
	vkCreateBuffer(m_device, ptr(VkBufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = 2,
		.pQueueFamilyIndices = ptr({ m_graphicsQueueFamily, m_transferQueueFamily })
		}), nullptr, &buffer.buffer);

	VkMemoryRequirements mrq;
	vkGetBufferMemoryRequirements(m_device, buffer.buffer, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? ptr(VkMemoryAllocateFlagsInfo{ .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT }) : nullptr,
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(memProps, mrq.memoryTypeBits)
		}), nullptr, &buffer.memory);
	vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0);

	if (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.hostPtr);
	}
	else if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		buffer.devicePtr = vkGetBufferDeviceAddress(m_device, ptr(VkBufferDeviceAddressInfo{
			.buffer = buffer.buffer
		}));
	}

	return buffer;
}

void Renderer::destroyBuffer(Buffer buffer) {
	vkDestroyBuffer(m_device, buffer.buffer, nullptr);
	vkFreeMemory(m_device, buffer.memory, nullptr);
}