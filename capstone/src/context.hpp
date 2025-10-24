#ifndef CONTEXT_H
#define CONTEXT_H

#include <tbrs/types.hpp>
#include <volk/volk.h>

#include "buffer.hpp"
#include "image.hpp"

class VulkanContext {
public:

	VkInstance instance();
	VkPhysicalDevice physicalDevice();
	VkDevice device();
	VkQueue graphicsQueue();
	VkQueue computeQueue();
	VkQueue transferQueue();
	u32* queueFamilies();
	u32 maxSampledDescriptors();
	u32 getMemoryIndex(VkMemoryPropertyFlags flags, u32 mask);

	Buffer createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
	Image createImage(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage, u32 mips = 1, b8 cube = false);

	VulkanContext();
	~VulkanContext();

private:
	u32 getQueue(VkQueueFlags include, VkQueueFlags exclude = 0);

	VkInstance m_instance;
	VkPhysicalDevice m_physicalDevice;
	VkPhysicalDeviceMemoryProperties m_memProps;
	VkDevice m_device;

	u32 m_queueFamilies[3];
	VkQueue m_graphicsQueue;
	VkQueue m_computeQueue;
	VkQueue m_transferQueue;

	u32 m_maxSampledImageDescriptors;
};

#endif