#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Renderer::run() {
	while(!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();

		f32 orthoSize = std::sqrt(2.0f);
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), static_cast<f32>(glfwGetTime()), glm::vec3(0.0f, 1.0f, 0.0f)) * m_model.baseTransform;
		glm::mat4 view = glm::lookAt(m_position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 projection = perspective(glm::radians(m_fov / 2.0f), static_cast<f32>(m_width) / static_cast<f32>(m_height), 0.1f);
		glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), m_lightAngle, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 lightProjection = ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, -orthoSize, orthoSize);
		glm::mat4 camMatrixNoTranslation = projection * glm::mat4(glm::mat3(view));

		PushConstants pushConstants = {
			m_oitBuffer.devicePtr,
			m_model.vertexBuffer.devicePtr,
			m_model.materialBuffer.devicePtr,
			m_poissonDiskBuffer.devicePtr,
			projection * view,
			lightProjection* lightView,
			model,
			glm::vec4(1.0f),
			m_position,
			m_lightAngle,
			m_width
		};

		auto frameData = m_perFrameData[m_frameIndex];
		vkWaitForFences(m_device, 1, &frameData.fence, true, std::numeric_limits<u64>::max());

		u32 imageIndex;
		VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<u64>::max(), frameData.acquireSem, nullptr, &imageIndex);
		if(result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapchain();
			continue;
		}

		vkResetFences(m_device, 1, &frameData.fence);
		vkResetCommandPool(m_device, frameData.cmdPool, 0);

		vkBeginCommandBuffer(frameData.cmdBuffer, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));

		vkCmdSetViewport(frameData.cmdBuffer, 0, 1, ptr(VkViewport{ 0.0f, 0.0f, static_cast<f32>(m_shadowMapSize), static_cast<f32>(m_shadowMapSize), 0.0f, 1.0f }));
		vkCmdSetScissor(frameData.cmdBuffer, 0, 1, ptr(VkRect2D{ { 0, 0 }, { static_cast<u32>(m_shadowMapSize), static_cast<u32>(m_shadowMapSize) } }));
		
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.image = m_shadowMap.image,
				.subresourceRange = depthSubresourceRange()
			})
		}));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_shadowMapSize), static_cast<u32>(m_shadowMapSize) } },
			.layerCount = 1,
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_shadowMap.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f }
			})
		}));

		if(m_model.numOpaqueDrawCommands > 0) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
			vkCmdBindIndexBuffer(frameData.cmdBuffer, m_model.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(frameData.cmdBuffer, m_modelPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, 0, m_model.numOpaqueDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		}

		vkCmdEndRendering(frameData.cmdBuffer);

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.image = m_shadowMap.image,
					.subresourceRange = depthSubresourceRange(),
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
					.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.image = m_depthTarget.image,
					.subresourceRange = depthSubresourceRange()
				}
			})
		}));

		vkCmdSetViewport(frameData.cmdBuffer, 0, 1, ptr(VkViewport{ 0.0f, 0.0f, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f }));
		vkCmdSetScissor(frameData.cmdBuffer, 0, 1, ptr(VkRect2D{ { 0, 0 }, { static_cast<u32>(m_width), static_cast<u32>(m_height) } }));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
			.layerCount = 1,
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_depthTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f }
			})
		}));

		if(m_model.numOpaqueDrawCommands > 0) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_prepassPipeline);
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, 0, m_model.numOpaqueDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		}

		vkCmdEndRendering(frameData.cmdBuffer);

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
					.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
					.image = m_depthTarget.image,
					.subresourceRange = depthSubresourceRange()
				}
			})
		}));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = ptr(VkRenderingAttachmentInfo{
				.imageView = m_colorTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f, 0.0f, 0.0f, 1.0f }
			}),
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_depthTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			})
		}));

		if(m_model.numOpaqueDrawCommands > 0) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_opaquePipeline);
			vkCmdBindDescriptorSets(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &m_model.texSet, 0, nullptr);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 1, 4, ptr({
				VkWriteDescriptorSet{
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.irradianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.radianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 2,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_brdfIntegralTex.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 3,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_shadowSampler,
						.imageView = m_shadowMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				}
			}));
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, 0, m_model.numOpaqueDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		}

		if(m_skybox.environmentMap.image != VkImage{}) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = ptr(VkDescriptorImageInfo{
					.sampler = m_skyboxSampler,
					.imageView = m_skybox.environmentMap.view,
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				})
			}));
			vkCmdPushConstants(frameData.cmdBuffer, m_skyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &camMatrixNoTranslation);
			vkCmdDraw(frameData.cmdBuffer, 36, 1, 0, 0);
		}

		vkCmdEndRendering(frameData.cmdBuffer);

		if(m_model.numBlendDrawCommands > 0) {
			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.bufferMemoryBarrierCount = 1,
				.pBufferMemoryBarriers = ptr(VkBufferMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					.buffer = m_oitBuffer.buffer,
					.size = VK_WHOLE_SIZE
				})
			}));
		
			vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
				.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
				.layerCount = 1,
				.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
					.imageView = m_depthTarget.view,
					.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				})
			}));
		
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blendPipeline);
		
			vkCmdBindDescriptorSets(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &m_model.texSet, 0, nullptr);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 1, 4, ptr({
				VkWriteDescriptorSet{
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.irradianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.radianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 2,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_brdfIntegralTex.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 3,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_shadowSampler,
						.imageView = m_shadowMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				}
			}));
			vkCmdPushConstants(frameData.cmdBuffer, m_modelPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
		
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, m_model.numOpaqueDrawCommands * sizeof(VkDrawIndexedIndirectCommand), m_model.numBlendDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		
			vkCmdEndRendering(frameData.cmdBuffer);
		
			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.bufferMemoryBarrierCount = 1,
				.pBufferMemoryBarriers = ptr(VkBufferMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					.buffer = m_oitBuffer.buffer,
					.size = VK_WHOLE_SIZE
				}),
			}));
		}
		else {
			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.bufferMemoryBarrierCount = 1,
				.pBufferMemoryBarriers = ptr(VkBufferMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					.buffer = m_oitBuffer.buffer,
					.size = VK_WHOLE_SIZE
				}),
			}));
		}

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_swapchainImages[imageIndex],
					.subresourceRange = colorSubresourceRange()
				}
			})
		}));
		
		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_postprocessingPipeline);

		VkDeviceAddress postprocessingPCs = m_model.numBlendDrawCommands != 0 ? m_oitBuffer.devicePtr : VkDeviceAddress{};
		vkCmdPushConstants(frameData.cmdBuffer, m_postprocessingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkDeviceAddress), &postprocessingPCs);
		
		vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_postprocessingPipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.descriptorCount = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr({
				VkDescriptorImageInfo{
					.imageView = m_colorTarget.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				},
				VkDescriptorImageInfo{
					.imageView = m_swapchainImageViews[imageIndex],
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}
			})
		}));
		
		vkCmdDispatch(frameData.cmdBuffer, (m_width + 7) / 8, (m_height + 7) / 8, 1);
		
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				.image = m_swapchainImages[imageIndex],
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkEndCommandBuffer(frameData.cmdBuffer);

		vkQueueSubmit2(m_graphicsQueue, 1, ptr(VkSubmitInfo2{
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
				.semaphore = frameData.acquireSem,
				.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
			}),
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{ .commandBuffer = frameData.cmdBuffer }),
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
				.semaphore = frameData.presentSem,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
			}),
		}), frameData.fence);

		result = vkQueuePresentKHR(m_graphicsQueue, ptr(VkPresentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &frameData.presentSem,
			.swapchainCount = 1,
			.pSwapchains = &m_swapchain,
			.pImageIndices = &imageIndex
		}));

		if(result != VK_SUCCESS || m_swapchainDirty) {
			recreateSwapchain();
		}

		m_frameIndex = (m_frameIndex + 1) % m_framesInFlight;
	}
}