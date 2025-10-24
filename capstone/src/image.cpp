#include "image.hpp"
#include "context.hpp"
#include <tbrs/vk_util.hpp>

VkImage Image::image() {
	return m_image;
}

VkImageView Image::view() {
	return m_view;
}

void Image::replaceView(VkImageView newView) {
	vkDestroyImageView(m_device, m_view, nullptr);
	m_view = newView;
}

Image::Image(VkDevice device, VkDeviceMemory mem, VkImage image, VkImageView view) :
	m_device(device), m_mem(mem), m_image(image), m_view(view) {}

Image::~Image() {
	if(m_device) {
		vkDestroyImageView(m_device, m_view, nullptr);
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

Image VulkanContext::createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips, b8 cube) {
	VkDeviceMemory mem;
	VkImage image;
	VkImageView view;

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
		.pQueueFamilyIndices = m_queueFamilies
		}), nullptr, &image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(m_device, image, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits)
		}), nullptr, &mem);
	vkBindImageMemory(m_device, image, mem, 0);

	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.pNext = srgbStorageImage ? ptr(VkImageViewUsageCreateInfo{.usage = usage & ~VK_IMAGE_USAGE_STORAGE_BIT }) : nullptr,
		.image = image,
		.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
		.format = srgbStorageImage ? VK_FORMAT_R8G8B8A8_SRGB : format,
		.subresourceRange = (format < 124 || format > 130) ? colorSubresourceRange() : depthSubresourceRange()
	}), nullptr, &view);

	return Image(m_device, mem, image, view);
}