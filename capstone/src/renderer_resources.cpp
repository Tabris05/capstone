#include "renderer.hpp"
#include <shared/oitnode.h>
#include <tbrs/vk_util.hpp>

void Renderer::createSwapchain() {
	VkSwapchainKHR oldSwapchain = m_swapchain;

	vkCreateSwapchainKHR(m_ctx.device(), ptr(VkSwapchainCreateInfoKHR{
		.surface = m_surface,
		.minImageCount = 3,
		.imageFormat = m_surfaceFormat.format,
		.imageColorSpace = m_surfaceFormat.colorSpace,
		.imageExtent = { m_window.width(), m_window.height() },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = m_ctx.queueFamilies(),
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = m_vsync ? VK_PRESENT_MODE_FIFO_KHR : m_nonVsyncPresentMode,
		.clipped = true,
		.oldSwapchain = oldSwapchain
	}), nullptr, &m_swapchain);
	vkDestroySwapchainKHR(m_ctx.device(), oldSwapchain, nullptr);

	u32 numSwapchainImages;
	vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &numSwapchainImages, nullptr);

	m_swapchainImages.resize(numSwapchainImages);
	vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &numSwapchainImages, m_swapchainImages.data());

	for(VkImage img : m_swapchainImages) {
		VkImageView curView;
		vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
			.image = img,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = m_surfaceFormat.format,
			.subresourceRange = colorSubresourceRange()
		}), nullptr, &curView);
		m_swapchainImageViews.push_back(curView);

		VkSemaphore curSem;
		vkCreateSemaphore(m_ctx.device(), ptr(VkSemaphoreCreateInfo{}), nullptr, &curSem);
		m_swapchainSems.push_back(curSem);
	}

	u32 numBloomMips = std::min(static_cast<u32>(std::floor(std::log2(std::max(m_window.width(), m_window.height()))) + 1), m_maxBloomMips);
	m_oitBuffer = createBuffer(m_window.width() * m_window.height() * 4 * sizeof(OITNode), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_colorTarget = createImage(m_window.width(), m_window.height(), m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	m_bloomTarget = createImage(m_window.width(), m_window.height(), m_colorFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, numBloomMips);
	m_depthTarget = createImage(m_window.width(), m_window.height(), m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	m_uiTarget = createImage(m_window.width(), m_window.height(), m_uiFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

	for(u8 i = 0; i < numBloomMips; i++) {
		VkImageView cur;
		vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
			.image = m_bloomTarget.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = m_colorFormat,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), nullptr, &cur);

		m_bloomMips.push_back(std::make_pair(cur, glm::uvec2(std::max(m_window.width() >> i, 1u), std::max(m_window.height() >> i, 1u))));
	}
}

void Renderer::recreateSwapchain() {
	m_window.updateSize();

	vkDeviceWaitIdle(m_ctx.device());

	for(auto [view, _] : m_bloomMips) {
		vkDestroyImageView(m_ctx.device(), view, nullptr);
	}
	m_bloomMips.clear();
	destroyBuffer(m_oitBuffer);
	destroyImage(m_colorTarget);
	destroyImage(m_bloomTarget);
	destroyImage(m_depthTarget);
	destroyImage(m_uiTarget);
	for(VkImageView view : m_swapchainImageViews) {
		vkDestroyImageView(m_ctx.device(), view, nullptr);
	}
	m_swapchainImageViews.clear();
	for(VkSemaphore sem : m_swapchainSems) {
		vkDestroySemaphore(m_ctx.device(), sem, nullptr);
	}
	m_swapchainSems.clear();

	createSwapchain();
	m_swapchainDirty = false;
}

Renderer::Image Renderer::createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips, b8 cube) {
	VkImageCreateFlags flags = 0;
	bool srgbStorageImage = format == VK_FORMAT_R8G8B8A8_SRGB && (usage & VK_IMAGE_USAGE_STORAGE_BIT);
	if(srgbStorageImage) {
		format = VK_FORMAT_R8G8B8A8_UNORM;
		flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

	Image image;
	vkCreateImage(m_ctx.device(), ptr(VkImageCreateInfo{
		.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : flags,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = mips,
		.arrayLayers = cube ? 6u : 1u,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = 3,
		.pQueueFamilyIndices = m_ctx.queueFamilies()
	}), nullptr, &image.image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(m_ctx.device(), image.image, &mrq);
	vkAllocateMemory(m_ctx.device(), ptr(VkMemoryAllocateInfo{
		.allocationSize = mrq.size,
		.memoryTypeIndex = m_ctx.getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits)
	}), nullptr, &image.memory);
	vkBindImageMemory(m_ctx.device(), image.image, image.memory, 0);

	vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
		.pNext = srgbStorageImage ? ptr(VkImageViewUsageCreateInfo{ .usage = usage & ~VK_IMAGE_USAGE_STORAGE_BIT }) : nullptr,
		.image = image.image,
		.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
		.format = srgbStorageImage ? VK_FORMAT_R8G8B8A8_SRGB : format,
		.subresourceRange = (format < 124 || format > 130) ? colorSubresourceRange() : depthSubresourceRange()
	}), nullptr, &image.view);

	return image;
}

void Renderer::destroyImage(Image image) {
	vkDestroyImageView(m_ctx.device(), image.view, nullptr);
	vkDestroyImage(m_ctx.device(), image.image, nullptr);
	vkFreeMemory(m_ctx.device(), image.memory, nullptr);
}

Renderer::Buffer Renderer::createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps) {
	Buffer buffer;
	vkCreateBuffer(m_ctx.device(), ptr(VkBufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = 3,
		.pQueueFamilyIndices = m_ctx.queueFamilies()
	}), nullptr, &buffer.buffer);

	VkMemoryRequirements mrq;
	vkGetBufferMemoryRequirements(m_ctx.device(), buffer.buffer, &mrq);
	vkAllocateMemory(m_ctx.device(), ptr(VkMemoryAllocateInfo{
		.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? ptr(VkMemoryAllocateFlagsInfo{.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT }) : nullptr,
		.allocationSize = mrq.size,
		.memoryTypeIndex = m_ctx.getMemoryIndex(memProps, mrq.memoryTypeBits)
	}), nullptr, &buffer.memory);
	vkBindBufferMemory(m_ctx.device(), buffer.buffer, buffer.memory, 0);

	if(memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(m_ctx.device(), buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.hostPtr);
	}
	else if(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		buffer.devicePtr = vkGetBufferDeviceAddress(m_ctx.device(), ptr(VkBufferDeviceAddressInfo{ .buffer = buffer.buffer }));
	}

	return buffer;
}

void Renderer::destroyBuffer(Buffer buffer) {
	vkDestroyBuffer(m_ctx.device(), buffer.buffer, nullptr);
	vkFreeMemory(m_ctx.device(), buffer.memory, nullptr);
}

VkPipeline Renderer::createComputePipeline(VkPipelineLayout layout, std::initializer_list<u32> cs) {
	VkPipeline ret;

	vkCreateComputePipelines(m_ctx.device(), nullptr, 1, ptr(VkComputePipelineCreateInfo{
		.stage = {
			.pNext = ptr(VkShaderModuleCreateInfo{
				.codeSize = cs.size() * sizeof(u32),
				.pCode = cs.begin()
			}),
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.pName = "main"
		},
		.layout = layout
	}), nullptr, &ret);

	return ret;
}

VkPipeline Renderer::createGraphicsPipeline(VkPipelineLayout layout, std::initializer_list<u32> vs, std::initializer_list<u32> fs, VkCullModeFlagBits cullMode, VkCompareOp compareOp, bool depthWrite, bool hasColorAttachment) {
	VkPipeline ret;

	vkCreateGraphicsPipelines(m_ctx.device(), nullptr, 1, ptr(VkGraphicsPipelineCreateInfo{
		.pNext = ptr(VkPipelineRenderingCreateInfo{
			.colorAttachmentCount = hasColorAttachment ? 1u : 0u,
			.pColorAttachmentFormats = &m_colorFormat,
			.depthAttachmentFormat = m_depthFormat
		}),
		.stageCount = fs.size() == 0 ? 1u : 2u,
		.pStages = ptr({
			VkPipelineShaderStageCreateInfo{
				.pNext = ptr(VkShaderModuleCreateInfo{
					.codeSize = vs.size() * sizeof(u32),
					.pCode = vs.begin()
				}),
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.pName = "main"
			},
			VkPipelineShaderStageCreateInfo{
				.pNext = ptr(VkShaderModuleCreateInfo{
					.codeSize = fs.size() * sizeof(u32),
					.pCode = fs.begin()
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