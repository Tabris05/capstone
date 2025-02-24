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
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
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
			.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
			.dstAccessMask = VK_ACCESS_2_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
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

	//Image uintCube = createImage(cubeSize, cubeSize, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 1, true);
	Image uintCube = createImage(cubeSize, cubeSize, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, true);
	//Image cube = createImage(cubeSize, cubeSize, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, true);

	vkBeginCommandBuffer(m_computeCmd, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = uintCube.image,
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
				.imageView = uintCube.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}
	}));
	vkCmdDispatch(m_computeCmd, (cubeSize + 7) / 8, (cubeSize + 7) / 8, 6);

	//vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
	//	.imageMemoryBarrierCount = 2,
	//	.pImageMemoryBarriers = ptr({
	//		VkImageMemoryBarrier2{
	//			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
	//			.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
	//			.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
	//			.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
	//			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
	//			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	//			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//			.image = uintCube.image,
	//			.subresourceRange = colorSubresourceRange()
	//		},
	//		VkImageMemoryBarrier2{
	//			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
	//			.srcAccessMask = VK_ACCESS_2_NONE,
	//			.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
	//			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
	//			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	//			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	//			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//			.image = cube.image,
	//			.subresourceRange = colorSubresourceRange()
	//		},
	//	})
	//}));
	//
	//vkCmdCopyImage(m_computeCmd, uintCube.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cube.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, ptr(VkImageCopy{
	//	.srcSubresource = colorSubresourceLayers(),
	//	.dstSubresource = colorSubresourceLayers(),
	//	.extent = { static_cast<u32>(cubeSize), static_cast<u32>(cubeSize), 1 }
	//}));
	//
	//vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
	//	.imageMemoryBarrierCount = 1,
	//	.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
	//		.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
	//		.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
	//		.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
	//		.dstAccessMask = VK_ACCESS_2_NONE,
	//		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	//		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	//		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//		.image = cube.image,
	//		.subresourceRange = colorSubresourceRange()
	//	})
	//}));
	vkCmdPipelineBarrier2(m_computeCmd, ptr(VkDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
			.dstAccessMask = VK_ACCESS_2_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = uintCube.image,
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
	//destroyImage(uintCube);

	//m_skybox = Skybox{ cube };
	m_skybox = Skybox{ uintCube };
}

void Renderer::destroySkybox(Skybox skybox) {
	destroyImage(skybox.environmentMap);
}