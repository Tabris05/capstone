#ifndef IMAGE_H
#define IMAGE_H

#include <volk/volk.h>
#include <tbrs/types.hpp>
#include <utility>
#include "context.hpp"

class Image {
public:
	VkImage image();
	VkImageViewCreateInfo viewCI();

	Image() = default;
	Image(VulkanContext& ctx, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips = 1, b8 cube = false);
	~Image();

	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	Image(Image&& src);
	Image& operator=(Image&& src);

private:
	VkDevice m_device = {};
	VkDeviceMemory m_mem = {};
	VkImage m_image = {};
	VkImageViewCreateInfo m_viewCI;
	VkImageViewUsageCreateInfo m_viewUsageCI;
};

#endif