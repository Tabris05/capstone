#include "buffer.hpp"
#include "context.hpp"
#include <utility>
#include <tbrs/types.hpp>
#include <tbrs/vk_util.hpp>


VkBuffer Buffer::buffer() {
	return m_buffer;
}

Buffer::Buffer(VulkanContext& ctx, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps) :
	m_device(ctx.device()) {

	vkCreateBuffer(m_device, ptr(VkBufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = 3,
		.pQueueFamilyIndices = ctx.queueFamilies()
	}), nullptr, &m_buffer);

	VkMemoryRequirements mrq;
	vkGetBufferMemoryRequirements(m_device, m_buffer, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? ptr(VkMemoryAllocateFlagsInfo{.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT }) : nullptr,
		.allocationSize = mrq.size,
		.memoryTypeIndex = ctx.getMemoryIndex(memProps, mrq.memoryTypeBits)
	}), nullptr, &m_mem);
	vkBindBufferMemory(m_device, m_buffer, m_mem, 0);

	if(memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(m_device, m_mem, 0, VK_WHOLE_SIZE, 0, &m_ptr);
	}
	else if(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		m_ptr = reinterpret_cast<void*>(vkGetBufferDeviceAddress(m_device, ptr(VkBufferDeviceAddressInfo{ .buffer = m_buffer })));
	}
}

Buffer::~Buffer() {
	if(m_device) {
		vkDestroyBuffer(m_device, m_buffer, nullptr);
		vkFreeMemory(m_device, m_mem, nullptr);
	}
}

Buffer::Buffer(Buffer&& src) {
	memcpy(this, &src, sizeof(Buffer));
	memset(&src, 0, sizeof(Buffer));
}

Buffer& Buffer::operator=(Buffer&& src) {
	this->~Buffer();
	new (this) Buffer(std::move(src)); return *this;
};
