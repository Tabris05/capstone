#include "buffer.hpp"
#include "context.hpp"
#include <utility>
#include <tbrs/types.hpp>
#include <tbrs/vk_util.hpp>


VkBuffer Buffer::buffer() {
	return m_buffer;
}

Buffer::Buffer(VkDevice device, VkDeviceMemory mem, VkBuffer buffer, void* ptr) :
	m_device(device), m_mem(mem), m_buffer(buffer), m_ptr(ptr) {}

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

Buffer VulkanContext::createBuffer(u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps) {
	VkDeviceMemory mem;
	VkBuffer buffer;
	void* addr = nullptr;

	vkCreateBuffer(m_device, ptr(VkBufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = 3,
		.pQueueFamilyIndices = m_queueFamilies
		}), nullptr, &buffer);

	VkMemoryRequirements mrq;
	vkGetBufferMemoryRequirements(m_device, buffer, &mrq);
	vkAllocateMemory(m_device, ptr(VkMemoryAllocateInfo{
		.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? ptr(VkMemoryAllocateFlagsInfo{.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT }) : nullptr,
		.allocationSize = mrq.size,
		.memoryTypeIndex = getMemoryIndex(memProps, mrq.memoryTypeBits)
		}), nullptr, &mem);
	vkBindBufferMemory(m_device, buffer, mem, 0);

	if(memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(m_device, mem, 0, VK_WHOLE_SIZE, 0, &addr);
	}
	else if(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		addr = reinterpret_cast<void*>(vkGetBufferDeviceAddress(m_device, ptr(VkBufferDeviceAddressInfo{ .buffer = buffer })));
	}

	return Buffer(m_device, mem, buffer, addr);
}