#ifndef BUFFER_H
#define BUFFER_H

#include <volk/volk.h>
#include "context.hpp"

class Buffer {
public:
	VkBuffer buffer();

	template<typename T>
	T* map() {
		return static_cast<T*>(m_ptr);
	}

	Buffer() = default;
	Buffer(VulkanContext& ctx, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
	~Buffer();

	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;
	Buffer(Buffer&& src);
	Buffer& operator=(Buffer&& src);

private:
	VkDevice m_device = {};
	VkDeviceMemory m_mem = {};
	VkBuffer m_buffer = {};
	void* m_ptr = {};
};

#endif