#include "image.hpp"
#include <tbrs/vk_util.hpp>

VkImage Image::image() {
	return m_image;
}

VkImageViewCreateInfo Image::viewCI() {
	return m_viewCI;
}

Image::Image(VulkanContext& ctx, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips, b8 cube) :
	m_device(ctx.device()) {

	VkImageCreateFlags flags = 0;
	bool srgbStorageImage = format == VK_FORMAT_R8G8B8A8_SRGB && (usage & VK_IMAGE_USAGE_STORAGE_BIT);
	if(srgbStorageImage) {
		format = VK_FORMAT_R8G8B8A8_UNORM;
		flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

	vkCreateImage(m_device, ptr(VkImageCreateInfo{
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
		.pQueueFamilyIndices = ctx.queueFamilies()
	}), nullptr, &m_image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(m_device, m_image, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.allocationSize = mrq.size,
		.memoryTypeIndex = ctx.getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits)
	}), nullptr, &m_mem);
	vkBindImageMemory(m_device, m_image, m_mem, 0);

	m_viewUsageCI = VkImageViewUsageCreateInfo{ .usage = usage & ~VK_IMAGE_USAGE_STORAGE_BIT };
	m_viewCI = VkImageViewCreateInfo{
		.pNext = srgbStorageImage ? &m_viewUsageCI : nullptr,
		.image = m_image,
		.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
		.format = srgbStorageImage ? VK_FORMAT_R8G8B8A8_SRGB : format,
		.subresourceRange = (format < 124 || format > 130) ? colorSubresourceRange() : depthSubresourceRange()
	};
}

Image::~Image() {
	if(m_device) {
		vkDestroyImage(m_device, m_image, nullptr);
		vkFreeMemory(m_device, m_mem, nullptr);
	}
}

Image::Image(Image&& src) {
	memcpy(this, &src, sizeof(Image));
	memset(&src, 0, sizeof(Image));
}

Image& Image::operator=(Image&& src) {
	this->~Image();
	new (this) Image(std::move(src)); return *this;
};
