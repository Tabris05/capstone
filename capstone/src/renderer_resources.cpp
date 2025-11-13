#include "renderer.hpp"
#include <shared/oitnode.h>
#include <tbrs/vk_util.hpp>

void Renderer::createSwapchain() {
	VkSwapchainKHR oldSwapchain = m_swapchain;

	vkCreateSwapchainKHR(m_ctx.device(), ptr(VkSwapchainCreateInfoKHR{
		.surface = m_surface,
		.minImageCount = 3,
		.imageFormat = VK_FORMAT_R8G8B8A8_UNORM,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = { m_window.width(), m_window.height() },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
		.imageSharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = 3,
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
		u32 handle = m_heap.allocImageHandle(ptr(VkImageViewCreateInfo{
			.image = img,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.subresourceRange = colorSubresourceRange()
		}), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		m_swapchainHandles.push_back(handle);

		VkSemaphore curSem;
		vkCreateSemaphore(m_ctx.device(), ptr(VkSemaphoreCreateInfo{}), nullptr, &curSem);
		m_swapchainSems.push_back(curSem);
	}

	u32 numBloomMips = std::min(static_cast<u32>(std::floor(std::log2(std::max(m_window.width(), m_window.height()))) + 1), m_maxBloomMips);
	m_oitBuffer = Buffer(m_ctx, m_window.width() * m_window.height() * 4 * sizeof(OITNode), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_colorTarget = Image(m_ctx, m_window.width(), m_window.height(), m_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	m_colorTargetHandleStorage = m_heap.allocImageHandle(ptr(m_colorTarget.viewCI()), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	m_colorTargetHandleSampled = m_heap.allocImageHandle(ptr(m_colorTarget.viewCI()), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	vkCreateImageView(m_ctx.device(), ptr(m_colorTarget.viewCI()), nullptr, &m_colorTargetView);
	m_depthTarget = Image(m_ctx, m_window.width(), m_window.height(), m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	vkCreateImageView(m_ctx.device(), ptr(m_depthTarget.viewCI()), nullptr, &m_depthTargetView);
	m_uiTarget = Image(m_ctx, m_window.width(), m_window.height(), m_uiFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	m_uiTargetHandle = m_heap.allocImageHandle(ptr(m_uiTarget.viewCI()), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	vkCreateImageView(m_ctx.device(), ptr(m_uiTarget.viewCI()), nullptr, &m_uiTargetView);
	m_bloomTarget = Image(m_ctx, m_window.width(), m_window.height(), m_colorFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, numBloomMips);

	for(u8 i = 0; i < numBloomMips; i++) {
		VkImageViewCreateInfo ci{
			.image = m_bloomTarget.image(),
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = m_colorFormat,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		};
		u32 storageHandle = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		u32 sampledHandle = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

		m_bloomMips.emplace_back(storageHandle, sampledHandle, glm::uvec2(std::max(m_window.width() >> i, 1u), std::max(m_window.height() >> i, 1u)));
	}
}

void Renderer::destroySwapchain() {
	for(auto mip : m_bloomMips) {
		m_heap.freeImageHandle(mip.storageHandle);
		m_heap.freeImageHandle(mip.sampledHandle);
	}
	m_bloomMips.clear();

	for(u32 handle : m_swapchainHandles) {
		m_heap.freeImageHandle(handle);
	}
	m_swapchainHandles.clear();
	for(VkSemaphore sem : m_swapchainSems) {
		vkDestroySemaphore(m_ctx.device(), sem, nullptr);
	}
	m_swapchainSems.clear();

	vkDestroyImageView(m_ctx.device(), m_colorTargetView, nullptr);
	vkDestroyImageView(m_ctx.device(), m_depthTargetView, nullptr);
	vkDestroyImageView(m_ctx.device(), m_uiTargetView, nullptr);

	m_heap.freeImageHandle(m_uiTargetHandle);
	m_heap.freeImageHandle(m_colorTargetHandleStorage);
	m_heap.freeImageHandle(m_colorTargetHandleSampled);
}

void Renderer::recreateSwapchain() {
	m_window.updateSize();

	vkDeviceWaitIdle(m_ctx.device());

	destroySwapchain();
	createSwapchain();
	m_swapchainDirty = false;
}