#ifndef IMAGE_H
#define IMAGE_H

#include <volk/volk.h>
#include <tbrs/types.hpp>
#include <utility>

class Image {
public:
	VkImage image();
	VkImageView view();
	void replaceView(VkImageView newView);

	Image() = default;
	Image(VkDevice device, VkDeviceMemory mem, VkImage image, VkImageView view);
	~Image();

	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	Image(Image&& src);
	Image& operator=(Image&& src);

private:
	VkDevice m_device = {};
	VkDeviceMemory m_mem = {};
	VkImage m_image = {};
	VkImageView m_view = {};
};

#endif