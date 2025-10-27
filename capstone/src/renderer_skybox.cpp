#include "renderer.hpp"
#include <stb/stb_image.h>
#include <tbrs/vk_util.hpp>

Renderer::Skybox Renderer::createSkybox(std::filesystem::path path) {
	i32 width;
	i32 height;
	f32* pixels = stbi_loadf(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha);
	i32 cubeSize = height / 2;
	u64 byteSize = width * height * 4 * sizeof(f32);
	Buffer stagingBuffer(m_ctx, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memcpy(stagingBuffer.map<void>(), pixels, byteSize);
	stbi_image_free(pixels);

	Image srcImg(m_ctx, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	u32 srcHandle = m_heap.allocImageHandle(ptr(srcImg.viewCI()), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

	vkBeginCommandBuffer(m_transferCmdSkyboxThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	vkCmdInitializeColorImage(m_transferCmdSkyboxThread, srcImg.image());
	vkCmdCopyBufferToImage(m_transferCmdSkyboxThread, stagingBuffer.buffer(), srcImg.image(), VK_IMAGE_LAYOUT_GENERAL, 1, ptr(VkBufferImageCopy{
		.imageSubresource = colorSubresourceLayers(),
		.imageExtent = { static_cast<u32>(width), static_cast<u32>(height), 1 }
	}));
	vkEndCommandBuffer(m_transferCmdSkyboxThread);

	m_dmaTransferLock.lock();
	vkQueueSubmit2(m_ctx.transferQueue(), 1, ptr(VkSubmitInfo2{
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_transferCmdSkyboxThread }),
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_skyboxSem.semaphore(),
			.value = m_skyboxSem.advance(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		})
	}), nullptr);
	m_dmaTransferLock.unlock();

	VkImageViewUsageCreateInfo usageCI{ .usage = VK_IMAGE_USAGE_SAMPLED_BIT };
	VkImageViewCreateInfo ci{
		.pNext = &usageCI,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	};

	u8 cubeMips = std::log2(cubeSize) + 1;
	Image environmentMap(m_ctx, cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cubeMips, true);
	u32 envMapHandleStorage = m_heap.allocImageHandle(ptr(environmentMap.viewCI()), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	ci.image = environmentMap.image();
	u32 envMapHandleSampled = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	Image irradianceMap(m_ctx, m_irradianceMapSize, m_irradianceMapSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, true);
	u32 irradMapHandleStorage = m_heap.allocImageHandle(ptr(irradianceMap.viewCI()), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	ci.image = irradianceMap.image();
	u32 irradMapHandleSampled = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	Image radianceMap(m_ctx, cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cubeMips, true);
	u32 radMapHandleStorage = m_heap.allocImageHandle(ptr(radianceMap.viewCI()), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	ci.image = radianceMap.image();
	u32 radMapHandleSampled = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

	vkBeginCommandBuffer(m_computeCmdSkyboxThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	vkCmdInitializeColorImage(m_computeCmdSkyboxThread, environmentMap.image());
	m_heap.bind(m_computeCmdSkyboxThread);


	ImageHandle envHandles[] = { ImageHandle(srcHandle, m_genericSamplerHandle), ImageHandle(envMapHandleStorage) };

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_cubePipeline);
	vkCmdPushConstants(m_computeCmdSkyboxThread, m_heap.layout(), VK_SHADER_STAGE_ALL, 0, sizeof(envHandles), envHandles);
	vkCmdDispatch(m_computeCmdSkyboxThread, (cubeSize + 7) / 8, (cubeSize + 7) / 8, 6);

	ci = VkImageViewCreateInfo{
		.image = environmentMap.image(),
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_R32_UINT,
		.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS }
	};
	std::vector<u32> mipHandles;
	u32 mip0Handle = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	mipHandles.push_back(mip0Handle);

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_cubeMipPipeline);
	for(u8 i = 1; i < cubeMips; i++) {
		vkCmdBarrier(m_computeCmdSkyboxThread, PipelineStage::ComputeWrite, PipelineStage::ComputeRead);

		ci.subresourceRange.baseMipLevel = i;
		u32 curHandle = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		u32 curMipHandles[] = { mipHandles.back(), curHandle };

		vkCmdPushConstants(m_computeCmdSkyboxThread, m_heap.layout(), VK_SHADER_STAGE_ALL, 0, sizeof(curMipHandles), curMipHandles);
		mipHandles.push_back(curHandle);

		vkCmdDispatch(m_computeCmdSkyboxThread, (std::max(cubeSize >> i, 1) + 7) / 8, (std::max(cubeSize >> i, 1) + 7) / 8, 6);
	}

	vkCmdInitializeColorImage(m_computeCmdSkyboxThread, irradianceMap.image());
	vkCmdBarrier(m_computeCmdSkyboxThread, PipelineStage::Compute, PipelineStage::ComputeRead);

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline);

	ImageHandle handles[] = { ImageHandle(envMapHandleSampled, m_genericSamplerHandle), ImageHandle(irradMapHandleStorage) };
	vkCmdPushConstants(m_computeCmdSkyboxThread, m_heap.layout(), VK_SHADER_STAGE_ALL, 0, sizeof(handles), handles);
	vkCmdDispatch(m_computeCmdSkyboxThread, (m_irradianceMapSize + 7) / 8, (m_irradianceMapSize + 7) / 8, 6);

	vkCmdInitializeColorImage(m_computeCmdSkyboxThread, radianceMap.image());

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_radiancePipeline);

	for(u8 i = 0; i < cubeMips; i++) {
		u32 curMipHandle = m_heap.allocImageHandle(ptr(VkImageViewCreateInfo{
			.image = radianceMap.image(),
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32_UINT,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		mipHandles.push_back(curMipHandle);

		handles[1] = ImageHandle(curMipHandle);
		vkCmdPushConstants(m_computeCmdSkyboxThread, m_heap.layout(), VK_SHADER_STAGE_ALL, 0, sizeof(handles), handles);
		vkCmdDispatch(m_computeCmdSkyboxThread, (std::max(cubeSize >> i, 1) + 7) / 8, (std::max(cubeSize >> i, 1) + 7) / 8, 6);
	}

	vkEndCommandBuffer(m_computeCmdSkyboxThread);

	m_asyncComputeLock.lock();
	vkQueueSubmit2(m_ctx.computeQueue(), 1, ptr(VkSubmitInfo2{
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_skyboxSem.semaphore(),
			.value = m_skyboxSem.submittedValue(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}),
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_computeCmdSkyboxThread }),
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_skyboxSem.semaphore(),
			.value = m_skyboxSem.advance(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}),
	}), nullptr);
	m_asyncComputeLock.unlock();

	m_skyboxSem.wait();
	vkResetCommandPool(m_ctx.device(), m_transferPoolSkyboxThread, 0);
	vkResetCommandPool(m_ctx.device(), m_computePoolSkyboxThread, 0);

	m_heap.freeImageHandle(srcHandle);
	m_heap.freeImageHandle(envMapHandleStorage);
	m_heap.freeImageHandle(irradMapHandleStorage);
	m_heap.freeImageHandle(radMapHandleStorage);
	for(u32 handle : mipHandles) {
		m_heap.freeImageHandle(handle);
	}

	Skybox skybox;
	skybox.heap = &m_heap;
	skybox.environmentMap = std::move(environmentMap);
	ci.image = skybox.environmentMap.image();
	skybox.environmentMapHandle = envMapHandleSampled;
	skybox.irradianceMap = std::move(irradianceMap);
	ci.image = skybox.irradianceMap.image();
	skybox.irradianceMapHandle = irradMapHandleSampled;
	skybox.radianceMap = std::move(radianceMap);
	ci.image = skybox.radianceMap.image();
	skybox.radianceMapHandle = radMapHandleSampled;

	return skybox;
}