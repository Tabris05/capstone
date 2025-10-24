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
	m_oitBuffer = m_ctx.createBuffer(m_window.width() * m_window.height() * 4 * sizeof(OITNode), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_colorTarget = m_ctx.createImage(m_window.width(), m_window.height(), m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	m_bloomTarget = m_ctx.createImage(m_window.width(), m_window.height(), m_colorFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, numBloomMips);
	m_depthTarget = m_ctx.createImage(m_window.width(), m_window.height(), m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	m_uiTarget = m_ctx.createImage(m_window.width(), m_window.height(), m_uiFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

	for(u8 i = 0; i < numBloomMips; i++) {
		VkImageView cur;
		vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
			.image = m_bloomTarget.image(),
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