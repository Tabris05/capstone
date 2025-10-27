#ifndef DESCRIPTOR_HEAP_H
#define DESCRIPTOR_HEAP_H

#include <volk/volk.h>
#include <tbrs/types.hpp>
#include <vector>
#include <utility>
#include <mutex>

class DescriptorHeap {
	public:
		VkPipelineLayout layout();
		void bind(VkCommandBuffer cmd, bool gfxQueue = false);
	
		u32 allocImageHandle(const VkImageViewCreateInfo* ci, VkDescriptorType type);
		u32 allocSamplerHandle(const VkSamplerCreateInfo* ci);

		void freeImageHandle(u32 handle);
		void freeSamplerHandle(u32 handle);

		DescriptorHeap(VkDevice device);
		~DescriptorHeap();
	
		DescriptorHeap(const DescriptorHeap&) = delete;
		DescriptorHeap& operator=(const DescriptorHeap&) = delete;
		DescriptorHeap(DescriptorHeap&& src);
		DescriptorHeap& operator=(DescriptorHeap&& src);

	private:
		using FreeRanges = std::vector<std::pair<u32, u32>>;
		u32 acquireHandle(FreeRanges& ranges);
		void releaseHandle(FreeRanges& ranges, u32 handle);

		VkDevice m_device = {};
		VkDescriptorSetLayout m_setLayout = {};
		VkPipelineLayout m_pipeLayout = {};
		VkDescriptorPool m_pool = {};
		VkDescriptorSet m_set = {};

		std::mutex m_imageMutex;
		FreeRanges m_imageFreeRanges;
		std::vector<VkImageView> m_imageViews;

		std::mutex m_samplerMutex;
		FreeRanges m_samplerFreeRanges;
		std::vector<VkSampler> m_samplers;
		
		static constexpr u32 m_imageHandleCount = 1 << 20;
		static constexpr u32 m_samplerHandleCount = 1 << 12;
};

#endif