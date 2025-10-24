#include "renderer.hpp"
#include <stb/stb_image.h>
#include <tbrs/vk_util.hpp>

Renderer::Skybox Renderer::createSkybox(std::filesystem::path path) {
	i32 width;
	i32 height;
	f32* pixels = stbi_loadf(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha);
	i32 cubeSize = height / 2;
	u64 byteSize = width * height * 4 * sizeof(f32);
	Buffer stagingBuffer = m_ctx.createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memcpy(stagingBuffer.map<void>(), pixels, byteSize);
	stbi_image_free(pixels);

	Image srcImg = m_ctx.createImage(width, height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

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

	u8 cubeMips = std::log2(cubeSize) + 1;
	Image environmentMap = m_ctx.createImage(cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cubeMips, true);
	Image irradianceMap = m_ctx.createImage(m_irradianceMapSize, m_irradianceMapSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, true);
	Image radianceMap = m_ctx.createImage(cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cubeMips, true);

	VkImageView environmentMapView;
	VkImageView irradianceMapView;
	VkImageView radianceMapView;
	vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
		.pNext = ptr(VkImageViewUsageCreateInfo{.usage = VK_IMAGE_USAGE_SAMPLED_BIT }),
		.image = environmentMap.image(),
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &environmentMapView);
	vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
		.pNext = ptr(VkImageViewUsageCreateInfo{.usage = VK_IMAGE_USAGE_SAMPLED_BIT }),
		.image = irradianceMap.image(),
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &irradianceMapView);
	vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
		.pNext = ptr(VkImageViewUsageCreateInfo{.usage = VK_IMAGE_USAGE_SAMPLED_BIT }),
		.image = radianceMap.image(),
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &radianceMapView);

	vkBeginCommandBuffer(m_computeCmdSkyboxThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));

	vkCmdInitializeColorImage(m_computeCmdSkyboxThread, environmentMap.image());

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_cubePipeline);
	vkCmdPushDescriptorSet(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 2, ptr({
		VkWriteDescriptorSet{
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.sampler = m_skyboxSampler,
				.imageView = srcImg.view(),
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		},
		VkWriteDescriptorSet{
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = environmentMap.view(),
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}
	}));
	vkCmdDispatch(m_computeCmdSkyboxThread, (cubeSize + 7) / 8, (cubeSize + 7) / 8, 6);

	std::vector<VkImageView> mipViews;
	VkImageView mip0View;
	vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
		.image = environmentMap.image(),
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_R32_UINT,
		.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS }
	}), nullptr, &mip0View);
	mipViews.push_back(mip0View);

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_cubeMipPipeline);
	for(u8 i = 1; i < cubeMips; i++) {
		vkCmdBarrier(m_computeCmdSkyboxThread, PipelineStage::ComputeWrite, PipelineStage::ComputeRead);

		VkImageView curMipView;
		vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
			.image = environmentMap.image(),
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32_UINT,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), nullptr, &curMipView);

		vkCmdPushDescriptorSet(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_twoImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.descriptorCount = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr({
				VkDescriptorImageInfo{
					.imageView = mipViews.back(),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				},
				VkDescriptorImageInfo{
					.imageView = curMipView,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}
			})
		}));

		mipViews.push_back(curMipView);

		vkCmdDispatch(m_computeCmdSkyboxThread, (std::max(cubeSize >> i, 1) + 7) / 8, (std::max(cubeSize >> i, 1) + 7) / 8, 6);
	}

	vkCmdInitializeColorImage(m_computeCmdSkyboxThread, irradianceMap.image());
	vkCmdBarrier(m_computeCmdSkyboxThread, PipelineStage::Compute, PipelineStage::ComputeRead);

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline);

	vkCmdPushDescriptorSet(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 2, ptr({
		VkWriteDescriptorSet{
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.sampler = m_skyboxSampler,
				.imageView = environmentMapView,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		},
		VkWriteDescriptorSet{
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = irradianceMap.view(),
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}
	}));

	vkCmdDispatch(m_computeCmdSkyboxThread, (m_irradianceMapSize + 7) / 8, (m_irradianceMapSize + 7) / 8, 6);


	vkCmdInitializeColorImage(m_computeCmdSkyboxThread, radianceMap.image());

	vkCmdBindPipeline(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_radiancePipeline);

	vkCmdPushDescriptorSet(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = ptr(VkDescriptorImageInfo{
			.sampler = m_skyboxSampler,
			.imageView = environmentMapView,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL
		})
	}));

	for(u8 i = 0; i < cubeMips; i++) {
		VkImageView curMipView;
		vkCreateImageView(m_ctx.device(), ptr(VkImageViewCreateInfo{
			.image = radianceMap.image(),
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32_UINT,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), nullptr, &curMipView);
		mipViews.push_back(curMipView);

		vkCmdPushDescriptorSet(m_computeCmdSkyboxThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = curMipView,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}));

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

	for(VkImageView view : mipViews) {
		vkDestroyImageView(m_ctx.device(), view, nullptr);
	}

	environmentMap.replaceView(environmentMapView);
	irradianceMap.replaceView(irradianceMapView);
	radianceMap.replaceView(radianceMapView);

	Skybox skybox;
	skybox.environmentMap = std::move(environmentMap);
	skybox.irradianceMap = std::move(irradianceMap);
	skybox.radianceMap = std::move(radianceMap);

	return skybox;
}