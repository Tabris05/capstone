#include "renderer.hpp"
#include <stb/stb_image.h>
#include <tbrs/vk_util.hpp>

void Renderer::createSkybox(std::filesystem::path path) {
	i32 width;
	i32 height;
	f32* pixels = stbi_loadf(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha);
	auto reason = stbi_failure_reason();
	i32 cubeSize = height / 2;
	u64 byteSize = width * height * 4 * sizeof(f32);
	Buffer stagingBuffer = createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memcpy(stagingBuffer.hostPtr, pixels, byteSize);
	stbi_image_free(pixels);

	Image srcImg = createImage(width, height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	vkBeginCommandBuffer(m_transferCmd, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	vkCmdPipelineBarrier2(m_transferCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
			.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = srcImg.image,
			.subresourceRange = colorSubresourceRange()
		})
	}));
	vkCmdCopyBufferToImage(m_transferCmd, stagingBuffer.buffer, srcImg.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, ptr(VkBufferImageCopy{
		.imageSubresource = colorSubresourceLayers(),
		.imageExtent = { static_cast<u32>(width), static_cast<u32>(height), 1 }
	}));
	vkCmdPipelineBarrier2(m_transferCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
			.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
			.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image = srcImg.image,
			.subresourceRange = colorSubresourceRange()
		})
	}));
	vkEndCommandBuffer(m_transferCmd);

	vkQueueSubmit2(m_transferQueue, 1, ptr(VkSubmitInfo2{
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_transferCmd }),
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_transferToComputeSem,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		})
	}), nullptr);

	u8 cubeMips = std::log2(cubeSize) + 1;
	Image environmentMap = createImage(cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cubeMips, true);
	Image irradianceMap = createImage(m_irradianceMapSize, m_irradianceMapSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, true);
	Image radianceMap = createImage(cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cubeMips, true);

	VkImageView environmentMapView;
	VkImageView irradianceMapView;
	VkImageView radianceMapView;
	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.pNext = ptr(VkImageViewUsageCreateInfo{.usage = VK_IMAGE_USAGE_SAMPLED_BIT }),
		.image = environmentMap.image,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &environmentMapView);
	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.pNext = ptr(VkImageViewUsageCreateInfo{.usage = VK_IMAGE_USAGE_SAMPLED_BIT }),
		.image = irradianceMap.image,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &irradianceMapView);
	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.pNext = ptr(VkImageViewUsageCreateInfo{.usage = VK_IMAGE_USAGE_SAMPLED_BIT }),
		.image = radianceMap.image,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		.subresourceRange = colorSubresourceRange()
	}), nullptr, &radianceMapView);

	vkBeginCommandBuffer(m_computeCmd, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = environmentMap.image,
			.subresourceRange = colorSubresourceRange()
		})
	}));

	vkCmdBindPipeline(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cubePipeline);
	vkCmdPushDescriptorSet(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 2, ptr({
		VkWriteDescriptorSet{
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.sampler = m_skyboxSampler,
				.imageView = srcImg.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			})
		},
		VkWriteDescriptorSet{
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = environmentMap.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}
	}));
	vkCmdDispatch(m_computeCmd, (cubeSize + 7) / 8, (cubeSize + 7) / 8, 6);

	std::vector<VkImageView> mipViews;
	VkImageView mip0View;
	vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
		.image = environmentMap.image,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = VK_FORMAT_R32_UINT,
		.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS }
	}), nullptr, &mip0View);
	mipViews.push_back(mip0View);

	vkCmdBindPipeline(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cubeMipPipeline);
	for(u8 i = 1; i < cubeMips; i++) {
		vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
				.image = environmentMap.image,
				.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, static_cast<u32>(i - 1), 1, 0, VK_REMAINING_ARRAY_LAYERS }
			})
		}));

		VkImageView curMipView;
		vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
			.image = environmentMap.image,
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32_UINT,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), nullptr, &curMipView);

		vkCmdPushDescriptorSet(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_twoImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
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

		vkCmdDispatch(m_computeCmd, (std::max(cubeSize >> i, 1) + 7) / 8, (std::max(cubeSize >> i, 1) + 7) / 8, 6);
	}

	vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 2,
		.pImageMemoryBarriers = ptr({
			VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = environmentMap.image,
				.subresourceRange = colorSubresourceRange()
			},
			VkImageMemoryBarrier2{
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = irradianceMap.image,
				.subresourceRange = colorSubresourceRange()
			}
		})
	}));

	vkCmdBindPipeline(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline);

	vkCmdPushDescriptorSet(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 2, ptr({
		VkWriteDescriptorSet{
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.sampler = m_skyboxSampler,
				.imageView = environmentMapView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			})
		},
		VkWriteDescriptorSet{
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = irradianceMap.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}
	}));

	vkCmdDispatch(m_computeCmd, (m_irradianceMapSize + 7) / 8, (m_irradianceMapSize + 7) / 8, 6);

	vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 2,
		.pImageMemoryBarriers = ptr({
			VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = irradianceMap.image,
				.subresourceRange = colorSubresourceRange()
			},
			VkImageMemoryBarrier2{
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = radianceMap.image,
				.subresourceRange = colorSubresourceRange()
			}
		})
	}));

	vkCmdBindPipeline(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_radiancePipeline);

	vkCmdPushDescriptorSet(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = ptr(VkDescriptorImageInfo{
			.sampler = m_skyboxSampler,
			.imageView = environmentMapView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		})
	}));

	for(u8 i = 0; i < cubeMips; i++) {
		VkImageView curMipView;
		vkCreateImageView(m_device, ptr(VkImageViewCreateInfo{
			.image = radianceMap.image,
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32_UINT,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, VK_REMAINING_ARRAY_LAYERS }
		}), nullptr, &curMipView);
		mipViews.push_back(curMipView);

		vkCmdPushDescriptorSet(m_computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneTexOneImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = curMipView,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}));

		vkCmdDispatch(m_computeCmd, (std::max(cubeSize >> i, 1) + 7) / 8, (std::max(cubeSize >> i, 1) + 7) / 8, 6);
	}

	vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image = radianceMap.image,
			.subresourceRange = colorSubresourceRange()
		})
	}));

	vkEndCommandBuffer(m_computeCmd);

	vkQueueSubmit2(m_computeQueue, 1, ptr(VkSubmitInfo2{
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_transferToComputeSem,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}),
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_computeCmd })
	}), nullptr);

	vkQueueWaitIdle(m_transferQueue);
	vkResetCommandPool(m_device, m_transferPool, 0);
	destroyBuffer(stagingBuffer);

	vkQueueWaitIdle(m_computeQueue);
	vkResetCommandPool(m_device, m_computePool, 0);
	destroyImage(srcImg);

	vkDestroyImageView(m_device, environmentMap.view, nullptr);
	vkDestroyImageView(m_device, irradianceMap.view, nullptr);
	vkDestroyImageView(m_device, radianceMap.view, nullptr);
	for(VkImageView view : mipViews) {
		vkDestroyImageView(m_device, view, nullptr);
	}

	environmentMap.view = environmentMapView;
	irradianceMap.view = irradianceMapView;
	radianceMap.view = radianceMapView;
	m_skybox = Skybox{ environmentMap, irradianceMap, radianceMap };
}

void Renderer::destroySkybox(Skybox skybox) {
	destroyImage(skybox.environmentMap);
	destroyImage(skybox.irradianceMap);
	destroyImage(skybox.radianceMap);
}