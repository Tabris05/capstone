#ifndef IMAGE_HANDLE_H
#define IMAGE_HANDLE_H

#include <tbrs/types.hpp>

class ImageHandle {
	public:
		ImageHandle() = default;
		ImageHandle(u32 storageHandle) : m_handle(storageHandle) {}
		ImageHandle(u32 sampledHandle, u32 samplerHandle) : m_handle((samplerHandle << 20) | sampledHandle) {}
	private:
		u32 m_handle = ~0u;
};

#endif