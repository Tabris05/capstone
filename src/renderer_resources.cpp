#include "renderer.hpp"
#include "../shared/oitnode.h"
#include <tbrs/vk_util.hpp>

void Renderer::createSwapchain() {
	VkSwapchainKHR oldSwapchain = m_swapchain;

	vkCreateSwapchainKHR(m_device, ptr(VkSwapchainCreateInfoKHR{
		.surface = m_surface,
		.minImageCount = 3,
		.imageFormat = m_surfaceFormat.format,
		.imageColorSpace = m_surfaceFormat.colorSpace,
		.imageExtent = { static_cast<u32>(m_width), static_cast<u32>(m_height) },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = ptr(m_graphicsQueueFamily),
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = m_vsync ? VK_PRESENT_MODE_FIFO_KHR : m_nonVsyncPresentMode,
		.clipped = true,
		.oldSwapchain = oldSwapchain
	}), nullptr, &m_swapchain);
	vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);

	u32 numSwapchainImages;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &numSwapchainImages, nullptr);

	m_swapchainImages.resize(numSwapchainImages);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &numSwapchainImages, m_swapchainImages.data());

	for(VkImage img : m_swapchainImages) {
		VkImageView cur;
		vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
			.image = img,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = m_surfaceFormat.format,
			.subresourceRange = colorSubresourceRange()
		}), nullptr, &cur);
		m_swapchainImageViews.push_back(cur);
	}

	u32 numBloomMips = std::min(static_cast<u32>(std::floor(std::log2(std::max(m_width, m_height))) + 1), m_maxBloomMips);
	m_oitBuffer = createBuffer(m_width * m_height * 4 * sizeof(OITNode), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_colorTarget = createImage(m_width, m_height, m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	m_bloomTarget = createImage(m_width, m_height, m_colorFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, numBloomMips);
	m_depthTarget = createImage(m_width, m_height, m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	m_uiTarget = createImage(m_width, m_height, m_uiFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

	for(u8 i = 0; i < numBloomMips; i++) {
		VkImageView cur;
		vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
			.image = m_bloomTarget.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = m_colorFormat,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), nullptr, &cur);

		m_bloomMips.push_back(std::make_pair(cur, glm::uvec2(std::max(m_width >> i, 1), std::max(m_height >> i, 1))));
	}
}

void Renderer::recreateSwapchain() {
	glfwGetFramebufferSize(m_window, &m_width, &m_height);
	while(m_width == 0 || m_height == 0) {
		glfwGetFramebufferSize(m_window, &m_width, &m_height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(m_device);

	for(auto [view, _] : m_bloomMips) {
		vkDestroyImageView(m_device, view, nullptr);
	}
	m_bloomMips.clear();
	destroyBuffer(m_oitBuffer);
	destroyImage(m_colorTarget);
	destroyImage(m_bloomTarget);
	destroyImage(m_depthTarget);
	destroyImage(m_uiTarget);
	for(VkImageView view : m_swapchainImageViews) {
		vkDestroyImageView(m_device, view, nullptr);
	}
	m_swapchainImageViews.clear();

	createSwapchain();
	m_swapchainDirty = false;
}

Renderer::Image Renderer::createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips, b8 cube) {
	VkSharingMode mode = VK_SHARING_MODE_EXCLUSIVE;
	std::vector<u32> queueFamilies{ m_graphicsQueueFamily };

	if((usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) == 0) {
		mode = VK_SHARING_MODE_CONCURRENT;
		queueFamilies.push_back(m_computeQueueFamily);
		queueFamilies.push_back(m_transferQueueFamily);
	}
	
	VkImageCreateFlags flags = 0;
	bool srgbStorageImage = format == VK_FORMAT_R8G8B8A8_SRGB && (usage & VK_IMAGE_USAGE_STORAGE_BIT);
	if(srgbStorageImage) {
		format = VK_FORMAT_R8G8B8A8_UNORM;
		flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

	Image image;
	vkCreateImage(m_device, ptr(VkImageCreateInfo{
		.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : flags,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = mips,
		.arrayLayers = cube ? 6u : 1u,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = usage,
		.sharingMode = mode,
		.queueFamilyIndexCount = static_cast<u32>(queueFamilies.size()),
		.pQueueFamilyIndices = queueFamilies.data()
	}), nullptr, &image.image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(m_device, image.image, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits)
	}), nullptr, &image.memory);
	vkBindImageMemory(m_device, image.image, image.memory, 0);

	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.pNext = srgbStorageImage ? ptr(VkImageViewUsageCreateInfo{ .usage = usage & ~VK_IMAGE_USAGE_STORAGE_BIT }) : nullptr,
		.image = image.image,
		.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
		.format = srgbStorageImage ? VK_FORMAT_R8G8B8A8_SRGB : format,
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
		.queueFamilyIndexCount = 3,
		.pQueueFamilyIndices = ptr({ m_graphicsQueueFamily, m_computeQueueFamily, m_transferQueueFamily })
	}), nullptr, &buffer.buffer);

	VkMemoryRequirements mrq;
	vkGetBufferMemoryRequirements(m_device, buffer.buffer, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? ptr(VkMemoryAllocateFlagsInfo{.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT }) : nullptr,
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(memProps, mrq.memoryTypeBits)
	}), nullptr, &buffer.memory);
	vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0);

	if(memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.hostPtr);
	}
	else if(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		buffer.devicePtr = vkGetBufferDeviceAddress(m_device, ptr(VkBufferDeviceAddressInfo{ .buffer = buffer.buffer }));
	}

	return buffer;
}

void Renderer::destroyBuffer(Buffer buffer) {
	vkDestroyBuffer(m_device, buffer.buffer, nullptr);
	vkFreeMemory(m_device, buffer.memory, nullptr);
}

VkPipeline Renderer::createComputePipeline(VkPipelineLayout layout, std::filesystem::path shaderPath) {
	VkPipeline ret;

	std::vector<u32> shaderSrc = getShaderSource(shaderPath);

	vkCreateComputePipelines(m_device, nullptr, 1, ptr(VkComputePipelineCreateInfo{
		.stage = {
			.pNext = ptr(VkShaderModuleCreateInfo{
				.codeSize = shaderSrc.size() * sizeof(u32),
				.pCode = shaderSrc.data()
			}),
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.pName = "main"
		},
		.layout = layout
	}), nullptr, &ret);

	return ret;
}

VkPipeline Renderer::createGraphicsPipeline(VkPipelineLayout layout, std::filesystem::path vsPath, std::filesystem::path fsPath, VkCullModeFlagBits cullMode, VkCompareOp compareOp, bool depthWrite, bool hasColorAttachment) {
	VkPipeline ret;

	std::vector<u32> vsSrc = getShaderSource(vsPath);
	std::vector<u32> fsSrc = fsPath.empty() ? std::vector<u32>{} : getShaderSource(fsPath);

	vkCreateGraphicsPipelines(m_device, nullptr, 1, ptr(VkGraphicsPipelineCreateInfo{
		.pNext = ptr(VkPipelineRenderingCreateInfo{
			.colorAttachmentCount = hasColorAttachment ? 1u : 0u,
			.pColorAttachmentFormats = &m_colorFormat,
			.depthAttachmentFormat = m_depthFormat
		}),
		.stageCount = fsPath.empty() ? 1u : 2u,
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
		.pInputAssemblyState = ptr(VkPipelineInputAssemblyStateCreateInfo{.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST }),
		.pViewportState = ptr(VkPipelineViewportStateCreateInfo{.viewportCount = 1, .scissorCount = 1 }),
		.pRasterizationState = ptr(VkPipelineRasterizationStateCreateInfo{.cullMode = static_cast<VkCullModeFlags>(cullMode), .lineWidth = 1.0f }),
		.pMultisampleState = ptr(VkPipelineMultisampleStateCreateInfo{.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT }),
		.pDepthStencilState = ptr(VkPipelineDepthStencilStateCreateInfo{.depthTestEnable = true, .depthWriteEnable = depthWrite, .depthCompareOp = compareOp }),
		.pColorBlendState = ptr(VkPipelineColorBlendStateCreateInfo{.attachmentCount = 1, .pAttachments = ptr(VkPipelineColorBlendAttachmentState{.colorWriteMask = colorComponentAll() })}),
		.pDynamicState = ptr(VkPipelineDynamicStateCreateInfo{.dynamicStateCount = 2, .pDynamicStates = ptr({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }) }),
		.layout = layout
	}), nullptr, &ret);

	return ret;
}