#include "descriptor_heap.hpp"
#include <utility>
#include <tbrs/vk_util.hpp>

VkPipelineLayout DescriptorHeap::layout() {
	return m_pipeLayout;
}

void DescriptorHeap::bind(VkCommandBuffer cmd, bool gfxQueue) {
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeLayout, 0, 1, &m_set, 0, nullptr);

	if(gfxQueue) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_set, 0, nullptr);
	}
}

u32 DescriptorHeap::allocImageHandle(const VkImageViewCreateInfo* ci, VkDescriptorType type) {
	std::scoped_lock lock{ m_imageMutex };
	u32 handle = acquireHandle(m_imageFreeRanges);

	vkCreateImageView(m_device, ci, nullptr, &m_imageViews[handle]);
	vkUpdateDescriptorSets(m_device, 1, ptr(VkWriteDescriptorSet{
		.dstSet = m_set,
		.dstBinding = 0,
		.dstArrayElement = handle,
		.descriptorCount = 1,
		.descriptorType = type,
		.pImageInfo = ptr(VkDescriptorImageInfo{
			.imageView = m_imageViews[handle],
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL
		})
	}), 0, nullptr);

	return handle;
}

void DescriptorHeap::freeImageHandle(u32 handle) {
	std::scoped_lock lock{ m_imageMutex };
	vkDestroyImageView(m_device, m_imageViews[handle], nullptr);
	releaseHandle(m_imageFreeRanges, handle);
}

u32 DescriptorHeap::allocSamplerHandle(const VkSamplerCreateInfo* ci) {
	std::scoped_lock lock{ m_samplerMutex };
	u32 handle = acquireHandle(m_samplerFreeRanges);

	vkCreateSampler(m_device, ci, nullptr, &m_samplers[handle]);
	vkUpdateDescriptorSets(m_device, 1, ptr(VkWriteDescriptorSet{
		.dstSet = m_set,
		.dstBinding = 1,
		.dstArrayElement = handle,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
		.pImageInfo = ptr(VkDescriptorImageInfo{
			.sampler = m_samplers[handle]
		})
	}), 0, nullptr);

	return handle;
}

void DescriptorHeap::freeSamplerHandle(u32 handle) {
	std::scoped_lock lock{ m_samplerMutex };
	vkDestroySampler(m_device, m_samplers[handle], nullptr);
	releaseHandle(m_samplerFreeRanges, handle);
}

DescriptorHeap::DescriptorHeap(VkDevice device)
	: m_device(device), m_imageViews(m_imageHandleCount), m_samplers(m_samplerHandleCount) {

	m_imageFreeRanges.emplace_back(0, m_imageHandleCount - 1);
	m_samplerFreeRanges.emplace_back(0, m_samplerHandleCount - 1);

	VkDescriptorType descTypes[] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };

	VkMutableDescriptorTypeListEXT typeList{
		.descriptorTypeCount = 2,
		.pDescriptorTypes = descTypes
	};

	VkMutableDescriptorTypeCreateInfoEXT mutableCI{
		.mutableDescriptorTypeListCount = 1,
		.pMutableDescriptorTypeLists = &typeList
	};

	vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
		.pNext = ptr(VkDescriptorSetLayoutBindingFlagsCreateInfo{
			.pNext = &mutableCI,
			.bindingCount = 2,
			.pBindingFlags = ptr<VkDescriptorBindingFlags>({
				VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
				VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
			})
		}),
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = 2,
		.pBindings = ptr({
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT,
				.descriptorCount = m_imageHandleCount,
				.stageFlags = VK_SHADER_STAGE_ALL,
			},
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
				.descriptorCount = m_samplerHandleCount,
				.stageFlags = VK_SHADER_STAGE_ALL,
			}
		})
	}), nullptr, &m_setLayout);

	vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
		.setLayoutCount = 1,
		.pSetLayouts = &m_setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = ptr(VkPushConstantRange{
			.stageFlags = VK_SHADER_STAGE_ALL,
			.offset = 0,
			.size = 256
		})
	}), nullptr, &m_pipeLayout);

	vkCreateDescriptorPool(m_device, ptr(VkDescriptorPoolCreateInfo{
		.pNext = &mutableCI,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 1,
		.poolSizeCount = 2,
		.pPoolSizes = ptr({
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_MUTABLE_EXT,
				.descriptorCount = m_imageHandleCount
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_SAMPLER,
				.descriptorCount = m_samplerHandleCount
			}
		})
	}), nullptr, &m_pool);

	vkAllocateDescriptorSets(m_device, ptr(VkDescriptorSetAllocateInfo{
		.descriptorPool = m_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &m_setLayout,
	}), &m_set);
}

DescriptorHeap::~DescriptorHeap() {
	if(m_device) {
		vkDestroyDescriptorPool(m_device, m_pool, nullptr);
		vkDestroyPipelineLayout(m_device, m_pipeLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
	}
}


DescriptorHeap::DescriptorHeap(DescriptorHeap&& src) {
	memcpy(this, &src, sizeof(DescriptorHeap));
	memset(&src, 0, sizeof(DescriptorHeap));
}

DescriptorHeap& DescriptorHeap::operator=(DescriptorHeap&& src) {
	this->~DescriptorHeap();
	new (this) DescriptorHeap(std::move(src)); return *this;
};

u32 DescriptorHeap::acquireHandle(FreeRanges& ranges) {
	u32 result = ranges.front().first;
	ranges.front().first++;

	if(ranges.front().first > ranges.front().second) {
		// if we store the ranges backwards this could be pop back
		ranges.erase(ranges.begin());
	}

	return result;
}

void DescriptorHeap::releaseHandle(FreeRanges& ranges, u32 handle) {
	for(size_t i = 0; i < ranges.size(); i++) {
		if(ranges[i].second == handle - 1) {
			ranges[i].second++;
			if(i < ranges.size() - 1 && ranges[i].second + 1 == ranges[i + 1].first) {
				ranges[i].second = ranges[i + 1].second;
				ranges.erase(ranges.begin() + i + 1);
			}
			return;
		}
		else if(ranges[i].first == handle + 1) {
			ranges[i].first--;
			return;
		}
		else if(ranges[i].first > handle) {
			ranges.emplace(ranges.begin() + i, handle, handle);
			return;
		}
	}

	ranges.emplace_back(handle, handle);
}