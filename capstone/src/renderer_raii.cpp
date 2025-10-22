#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <nfd/nfd_glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <random>
#include <numbers>
#include <fstream>
#include <shared/vertex.h>

//shaders
#include <shaders/blend_frag.h>
#include <shaders/blit_comp.h>
#include <shaders/bloomdownsample_comp.h>
#include <shaders/bloomupsample_comp.h>
#include <shaders/brdfintegral_comp.h>
#include <shaders/cube_comp.h>
#include <shaders/cubemip_comp.h>
#include <shaders/fxaa_comp.h>
#include <shaders/irradiance_comp.h>
#include <shaders/mip_comp.h>
#include <shaders/model_vert.h>
#include <shaders/opaque_frag.h>
#include <shaders/postprocess_comp.h>
#include <shaders/prepass_vert.h>
#include <shaders/radiance_comp.h>
#include <shaders/shadow_vert.h>
#include <shaders/skybox_frag.h>
#include <shaders/skybox_vert.h>
#include <shaders/srgbmip_comp.h>
#include <shaders/transparencycomposite_comp.h>
#include <shaders/uicomposite_comp.h>

#include <resources/roboto_regular.h>

Renderer::Renderer(const char* path) {
	// glfw and NFD
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
		m_width = mode->width * 3 / 4;
		m_height = mode->height * 3 / 4;

		m_window = glfwCreateWindow(m_width, m_height, "Capstone", nullptr, nullptr);

		glfwSetWindowUserPointer(m_window, this);
		glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, i32 width, i32 height) {
			reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window))->onResize();
		});
		glfwSetScrollCallback(m_window, [](GLFWwindow* window, f64 xOffset, f64 yOffset) {
			reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window))->onScroll(yOffset);
		});

		NFD_Init();
		NFD_GetNativeWindowFromGLFWWindow(m_window, &m_nativeHandle);
	}

	// volk and VkInstance
	{
		volkInitialize();

		u32 glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		vkCreateInstance(ptr(VkInstanceCreateInfo{
			.pApplicationInfo = ptr(VkApplicationInfo{ .apiVersion = VK_API_VERSION_1_4 }),
			.enabledExtensionCount = glfwExtensionCount,
			.ppEnabledExtensionNames = glfwExtensions
		}), nullptr, &m_instance);

		volkLoadInstanceOnly(m_instance);
	}

	// VkPhysicalDevice, VkPhysicalDeviceMemoryProperties, and VkPhysicalDeviceProperties::limits::maxPerStageDescriptorSampledImages
	{
		vkEnumeratePhysicalDevices(m_instance, ptr(1u), &m_physicalDevice);
		vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProps);

		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
		m_maxSampledImageDescriptors = std::min(props.limits.maxPerStageDescriptorSampledImages, props.limits.maxPerStageDescriptorSamplers) - 4;
	}

	// VkDevice and VkQueues
	{
		m_graphicsQueueFamily = getQueue(VK_QUEUE_GRAPHICS_BIT);
		m_computeQueueFamily = getQueue(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
		m_transferQueueFamily = getQueue(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
		vkCreateDevice(m_physicalDevice, ptr(VkDeviceCreateInfo{
			.pNext = ptr(VkPhysicalDeviceFeatures2{
				.pNext = ptr(VkPhysicalDeviceVulkan11Features{
					.pNext = ptr(VkPhysicalDeviceVulkan12Features{
						.pNext = ptr(VkPhysicalDeviceVulkan13Features{
							.pNext = ptr(VkPhysicalDeviceVulkan14Features{
								.pNext = ptr(VkPhysicalDeviceRobustness2FeaturesEXT{
									.pNext = ptr(VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT{
										.pNext = ptr(VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR{.shaderMaximalReconvergence = true }),
										.fragmentShaderPixelInterlock = true
									}),
									.nullDescriptor = true
								}),
								.maintenance5 = true,
								.pushDescriptor = true,
							}),
							.synchronization2 = true,
							.dynamicRendering = true
						}),
						.shaderSampledImageArrayNonUniformIndexing = true,
						.descriptorBindingVariableDescriptorCount = true,
						.runtimeDescriptorArray = true,
						.scalarBlockLayout = true,
						.timelineSemaphore = true,
						.bufferDeviceAddress = true,
						.vulkanMemoryModel = true,
						.vulkanMemoryModelDeviceScope = true,
						.vulkanMemoryModelAvailabilityVisibilityChains = true
					}),
					.shaderDrawParameters = true,
				}),
				.features{
					.multiDrawIndirect = true,
					.drawIndirectFirstInstance = true,
					.samplerAnisotropy = true,
					.fragmentStoresAndAtomics = true,
					.shaderStorageImageReadWithoutFormat = true,
					.shaderStorageImageWriteWithoutFormat = true,
					.shaderInt64 = true
				}
			}),
			.queueCreateInfoCount = 3,
			.pQueueCreateInfos = ptr({
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_graphicsQueueFamily,
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				},
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_computeQueueFamily,
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				},
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_transferQueueFamily,
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				}
			}),
			.enabledExtensionCount = 4,
			.ppEnabledExtensionNames = ptr<const char*>({
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
				VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,
				VK_KHR_SHADER_MAXIMAL_RECONVERGENCE_EXTENSION_NAME
			}),
		}), nullptr, &m_device);

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
		vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);
	}

	// per-frame data (vk::CommandPool, vk::CommandBuffer, vk::Semaphores, vk::Fence)
	{
		for(u8 i = 0; i < m_framesInFlight; i++) {
			vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
				.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
				.queueFamilyIndex = m_graphicsQueueFamily
			}), nullptr, &m_perFrameData[i].cmdPool);
			vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
				.commandPool = m_perFrameData[i].cmdPool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1
			}), &m_perFrameData[i].cmdBuffer);
			vkCreateSemaphore(m_device, ptr(VkSemaphoreCreateInfo{}), nullptr, &m_perFrameData[i].acquireSem);
		}

		m_renderSem = Semaphore(m_device);
	}

	// VkSurface and VkSwapchain
	{
		glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface);

		u32 count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, formats.data());
		m_surfaceFormat = formats.front();
		for(auto format : formats) {
			if(!vkuFormatIsSRGB(format.format)) {
				m_surfaceFormat = format;
				break;
			}
		}

		count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &count, nullptr);
		std::vector<VkPresentModeKHR> modes(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &count, modes.data());
		
		for(auto mode : modes) {
			if(mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				m_nonVsyncPresentMode == VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			}
		}

		createSwapchain();
	}

	// transfer objects
	{
		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_transferQueueFamily
		}), nullptr, &m_transferPoolModelThread);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_transferPoolModelThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_transferCmdModelThread);

		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_transferQueueFamily
			}), nullptr, &m_transferPoolSkyboxThread);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_transferPoolSkyboxThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
			}), &m_transferCmdSkyboxThread);

		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_computeQueueFamily
		}), nullptr, &m_computePoolModelThread);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_computePoolModelThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_computeCmdModelThread);

		vkCreateCommandPool(m_device, ptr(VkCommandPoolCreateInfo{
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_computeQueueFamily
			}), nullptr, &m_computePoolSkyboxThread);
		vkAllocateCommandBuffers(m_device, ptr(VkCommandBufferAllocateInfo{
			.commandPool = m_computePoolSkyboxThread,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}), &m_computeCmdSkyboxThread);

		m_modelSem = Semaphore(m_device);
		m_skyboxSem = Semaphore(m_device);
	}

	// compute pipeline layouts
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			})
		}), nullptr, &m_oneImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_oneImageSetLayout
		}), nullptr, &m_oneImagePipelineLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 2,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				}
			})
		}), nullptr, &m_twoImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_twoImageSetLayout
		}), nullptr, &m_twoImagePipelineLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 2,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				}
			})
		}), nullptr, &m_oneTexOneImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_oneTexOneImageSetLayout
		}), nullptr, &m_oneTexOneImagePipelineLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_oneTexOneImageSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = sizeof(u32)
			})
		}), nullptr, &m_bloomPipelineLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 3,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
				}
			})
		}), nullptr, &m_threeImageSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_twoImageSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = sizeof(u32)
			})
		}), nullptr, &m_postprocessingPipelineLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_oneImageSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = sizeof(VkDeviceAddress)
			})
		}), nullptr, &m_transparencyCompositePipelineLayout);
	}

	// compute pipelines
	{
		m_mipPipeline = createComputePipeline(m_twoImagePipelineLayout, mip_comp);
		m_srgbMipPipeline = createComputePipeline(m_twoImagePipelineLayout, srgbmip_comp);
		m_cubePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, cube_comp);
		m_cubeMipPipeline = createComputePipeline(m_twoImagePipelineLayout, cubemip_comp);
		m_irradiancePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, irradiance_comp);
		m_radiancePipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, radiance_comp);
		m_brdfIntegralPipeline = createComputePipeline(m_oneImagePipelineLayout, brdfintegral_comp);
		m_transparencyCompositePipeline = createComputePipeline(m_transparencyCompositePipelineLayout, transparencycomposite_comp);
		m_postprocessingPipeline = createComputePipeline(m_postprocessingPipelineLayout, postprocess_comp);
		m_uiCompositePipeline = createComputePipeline(m_twoImagePipelineLayout, uicomposite_comp);
		m_bloomDownsamplePipeline = createComputePipeline(m_bloomPipelineLayout, bloomdownsample_comp);
		m_bloomUpsamplePipeline = createComputePipeline(m_bloomPipelineLayout, bloomupsample_comp);
		m_fxaaPipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, fxaa_comp);
		m_blitPipeline = createComputePipeline(m_oneTexOneImagePipelineLayout, blit_comp);
	}

	// global samplers
	{
		vkCreateSampler(m_device, ptr(VkSamplerCreateInfo{
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.anisotropyEnable = true,
			.maxAnisotropy = 16,
			.maxLod = VK_LOD_CLAMP_NONE
		}), nullptr, &m_skyboxSampler);

		vkCreateSampler(m_device, ptr(VkSamplerCreateInfo{
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.compareEnable = true,
			.compareOp = VK_COMPARE_OP_GREATER
		}), nullptr, &m_shadowSampler);
	}

	// generate brdf integral tex
	{
		m_brdfIntegralTex = createImage(m_brdfIntegralLUTSize, m_brdfIntegralLUTSize, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		vkBeginCommandBuffer(m_computeCmdModelThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
		
		vkCmdPipelineBarrier2(m_computeCmdModelThread, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = m_brdfIntegralTex.image,
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkCmdBindPipeline(m_computeCmdModelThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfIntegralPipeline);

		vkCmdPushDescriptorSet(m_computeCmdModelThread, VK_PIPELINE_BIND_POINT_COMPUTE, m_oneImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr(VkDescriptorImageInfo{
				.imageView = m_brdfIntegralTex.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			})
		}));

		vkCmdDispatch(m_computeCmdModelThread, (m_brdfIntegralLUTSize + 7) / 8, (m_brdfIntegralLUTSize + 7) / 8, 1);

		vkEndCommandBuffer(m_computeCmdModelThread);

		vkQueueSubmit2(m_computeQueue, 1, ptr(VkSubmitInfo2{
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_computeCmdModelThread })
		}), nullptr);

		vkQueueWaitIdle(m_computeQueue);
		vkResetCommandPool(m_device, m_computePoolModelThread, 0);
	}

	// Allocate Shadow Map
	{
		m_shadowMap = createImage(m_shadowMapSize, m_shadowMapSize, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		
		u32 poissonDiskBufferSize = m_poissonDiskWindowSize * m_poissonDiskWindowSize * m_poissonDiskFilterSize * m_poissonDiskFilterSize * sizeof(glm::vec2);

		std::vector<glm::vec2> samples;
		samples.reserve(poissonDiskBufferSize / sizeof(glm::vec2));

		std::default_random_engine generator;
		std::uniform_real_distribution<f32> distribution(-0.5f, 0.5f);

		for(i32 v = m_poissonDiskFilterSize - 1; v >= 0; v--) {
			for(i32 u = 0; u < m_poissonDiskFilterSize; u++) {
				for(i32 i = 0; i < m_poissonDiskWindowSize * m_poissonDiskWindowSize; i++) {
					f32 x = (u + 0.5f + distribution(generator)) / m_poissonDiskFilterSize;
					f32 y = (v + 0.5f + distribution(generator)) / m_poissonDiskFilterSize;
					
					glm::vec2 sample(std::sqrtf(y) * std::cosf(2.0f * std::numbers::pi_v<f32> * x), std::sqrtf(y) * std::sinf(2.0f * std::numbers::pi_v<f32> * x));
					samples.push_back(sample);
				}
			}
		}

		Buffer poissonDiskStagingBuffer = createBuffer(poissonDiskBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		m_poissonDiskBuffer = createBuffer(poissonDiskBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		memcpy(poissonDiskStagingBuffer.hostPtr, samples.data(), poissonDiskBufferSize);

		vkBeginCommandBuffer(m_transferCmdModelThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
		vkCmdCopyBuffer(m_transferCmdModelThread, poissonDiskStagingBuffer.buffer, m_poissonDiskBuffer.buffer, 1, ptr(VkBufferCopy{ .size = poissonDiskBufferSize }));
		vkEndCommandBuffer(m_transferCmdModelThread);

		vkQueueSubmit2(m_transferQueue, 1, ptr(VkSubmitInfo2{
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_transferCmdModelThread })
		}), nullptr);

		vkQueueWaitIdle(m_transferQueue);
		vkResetCommandPool(m_device, m_transferPoolModelThread, 0);
		
		destroyBuffer(poissonDiskStagingBuffer);
	}

	// Color Pass Pipelines
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.pNext = ptr(VkDescriptorSetLayoutBindingFlagsCreateInfo{
				.bindingCount = 1,
				.pBindingFlags = ptr<VkDescriptorBindingFlags>(VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
			}),
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = m_maxSampledImageDescriptors,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
			})
		}), nullptr, &m_modelSetLayout);

		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 4,
			.pBindings = ptr({
				VkDescriptorSetLayoutBinding{
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				},
				VkDescriptorSetLayoutBinding{
					.binding = 3,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
				}
			})
		}), nullptr, &m_modelPushDescriptorLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 2,
			.pSetLayouts = ptr({ m_modelSetLayout, m_modelPushDescriptorLayout }),
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				.offset = 0,
				.size = sizeof(PushConstants),
			})
		}), nullptr, &m_modelPipelineLayout);

		m_opaquePipeline = createGraphicsPipeline(m_modelPipelineLayout, model_vert, opaque_frag, VK_CULL_MODE_BACK_BIT, VK_COMPARE_OP_EQUAL, false, true);
		m_blendPipeline = createGraphicsPipeline(m_modelPipelineLayout, model_vert, blend_frag, VK_CULL_MODE_NONE, VK_COMPARE_OP_GREATER, false, false);
	}

	// Depth Only Pipelines
	{
		m_prepassPipeline = createGraphicsPipeline(m_modelPipelineLayout, prepass_vert, {}, VK_CULL_MODE_BACK_BIT, VK_COMPARE_OP_GREATER, true, false);
		m_shadowPipeline = createGraphicsPipeline(m_modelPipelineLayout, shadow_vert, {}, VK_CULL_MODE_BACK_BIT, VK_COMPARE_OP_GREATER, true, false);
	}

	// Skybox Pipeline
	{
		vkCreateDescriptorSetLayout(m_device, ptr(VkDescriptorSetLayoutCreateInfo{
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = 1,
			.pBindings = ptr(VkDescriptorSetLayoutBinding{
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
			})
		}), nullptr, &m_skyboxSetLayout);

		vkCreatePipelineLayout(m_device, ptr(VkPipelineLayoutCreateInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &m_skyboxSetLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = ptr(VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.offset = 0,
				.size = sizeof(glm::mat4),
			})
		}), nullptr, &m_skyboxPipelineLayout);

		m_skyboxPipeline = createGraphicsPipeline(m_skyboxPipelineLayout, skybox_vert, skybox_frag, VK_CULL_MODE_NONE, VK_COMPARE_OP_EQUAL, false, true);
	}

	// ImGui
	{
		ImGui::CreateContext();
		ImGui_ImplGlfw_InitForVulkan(m_window, true);
		ImGui_ImplVulkan_Init(ptr(ImGui_ImplVulkan_InitInfo{
			.ApiVersion = VK_API_VERSION_1_4,
			.Instance = m_instance,
			.PhysicalDevice = m_physicalDevice,
			.Device = m_device,
			.QueueFamily = m_graphicsQueueFamily,
			.Queue = m_graphicsQueue,
			.MinImageCount = 3,
			.ImageCount = 3,
			.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE,
			.UseDynamicRendering = true,
			.PipelineRenderingCreateInfo{
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &m_uiFormat
			}
		}));
	}

	// ImGui Styling
	{
		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();

		ImFontConfig cfg;
		cfg.FontDataOwnedByAtlas = false;
		io.Fonts->AddFontFromMemoryTTF(roboto_regular, sizeof(roboto_regular), 18, &cfg);
		m_largeFont = io.Fonts->AddFontFromMemoryTTF(roboto_regular, sizeof(roboto_regular), 36, &cfg);

		style.WindowMinSize = ImVec2(160, 20);
		style.FramePadding = ImVec2(4, 2);
		style.ItemSpacing = ImVec2(6, 2);
		style.ItemInnerSpacing = ImVec2(6, 4);
		style.Alpha = 0.95f;
		style.WindowRounding = 4.0f;
		style.FrameRounding = 2.0f;
		style.IndentSpacing = 6.0f;
		style.ItemInnerSpacing = ImVec2(2, 4);
		style.ColumnsMinSpacing = 50.0f;
		style.GrabMinSize = 14.0f;
		style.GrabRounding = 16.0f;
		style.ScrollbarSize = 12.0f;
		style.ScrollbarRounding = 16.0f;

		style.Colors[ImGuiCol_Text] = ImVec4(0.86f, 0.93f, 0.89f, 0.78f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.47f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.92f, 0.18f, 0.29f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.77f, 0.83f, 0.04f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.92f, 0.18f, 0.29f, 0.43f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.9f);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.73f);
	}

	if(path) {
		m_loadingScene = true;
		m_loadingSceneFuture = std::async(std::launch::async, [this, path] {
			std::ifstream file(path, std::ios::binary);

			auto readVariable = [&file](auto& var) {
				file.read(reinterpret_cast<char*>(&var), sizeof(var));
			};

			readVariable(m_scroll);
			readVariable(m_position);
			readVariable(m_rotation);
			readVariable(m_vsync);
			readVariable(m_camIdx);
			readVariable(m_colorIdx);
			readVariable(m_AAIdx);
			readVariable(m_fov);
			readVariable(m_sensitivity);
			readVariable(m_scrollSensitivity);
			readVariable(m_lightPitch);
			readVariable(m_lightYaw);
			readVariable(m_modelPitch);
			readVariable(m_modelYaw);
			readVariable(m_modelRoll);
			readVariable(m_modelScale);
			readVariable(m_lightAngle);
			readVariable(m_lightColor);

			u64 size = 0;
			readVariable(size);
			m_modelPath.resize(size);
			file.read(m_modelPath.data(), size);

			readVariable(size);
			m_skyboxPath.resize(size);
			file.read(m_skyboxPath.data(), size);
		});
	}
}

Renderer::~Renderer() {
	vkDeviceWaitIdle(m_device);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	for(u8 i = 0; i < m_framesInFlight; i++) {
		vkDestroyCommandPool(m_device, m_perFrameData[i].cmdPool, nullptr);
		vkDestroySemaphore(m_device, m_perFrameData[i].acquireSem, nullptr);
	}
	m_renderSem.destroy();

	vkDestroyCommandPool(m_device, m_transferPoolSkyboxThread, nullptr);
	vkDestroyCommandPool(m_device, m_computePoolSkyboxThread, nullptr);
	m_skyboxSem.destroy();

	vkDestroyCommandPool(m_device, m_transferPoolModelThread, nullptr);
	vkDestroyCommandPool(m_device, m_computePoolModelThread, nullptr);
	m_modelSem.destroy();

	vkDestroyPipeline(m_device, m_blitPipeline, nullptr);
	vkDestroyPipeline(m_device, m_fxaaPipeline, nullptr);
	vkDestroyPipeline(m_device, m_bloomUpsamplePipeline, nullptr);
	vkDestroyPipeline(m_device, m_bloomDownsamplePipeline, nullptr);
	vkDestroyPipeline(m_device, m_uiCompositePipeline, nullptr);
	vkDestroyPipeline(m_device, m_postprocessingPipeline, nullptr);
	vkDestroyPipeline(m_device, m_transparencyCompositePipeline, nullptr);
	vkDestroyPipeline(m_device, m_brdfIntegralPipeline, nullptr);
	vkDestroyPipeline(m_device, m_radiancePipeline, nullptr);
	vkDestroyPipeline(m_device, m_irradiancePipeline, nullptr);
	vkDestroyPipeline(m_device, m_cubeMipPipeline, nullptr);
	vkDestroyPipeline(m_device, m_cubePipeline, nullptr);
	vkDestroyPipeline(m_device, m_mipPipeline, nullptr);
	vkDestroyPipeline(m_device, m_srgbMipPipeline, nullptr);
	vkDestroyPipeline(m_device, m_skyboxPipeline, nullptr);
	
	vkDestroyPipelineLayout(m_device, m_postprocessingPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_threeImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_oneTexOneImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_oneTexOneImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_bloomPipelineLayout, nullptr);
	vkDestroyPipelineLayout(m_device, m_twoImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_twoImageSetLayout, nullptr);

	vkDestroyPipelineLayout(m_device, m_transparencyCompositePipelineLayout, nullptr);
	vkDestroyPipelineLayout(m_device, m_oneImagePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_oneImageSetLayout, nullptr);


	vkDestroyPipelineLayout(m_device, m_skyboxPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_skyboxSetLayout, nullptr);

	vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
	vkDestroyPipeline(m_device, m_prepassPipeline, nullptr);
	vkDestroyPipeline(m_device, m_blendPipeline, nullptr);
	vkDestroyPipeline(m_device, m_opaquePipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_modelPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_modelSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_modelPushDescriptorLayout, nullptr);

	vkDestroySampler(m_device, m_shadowSampler, nullptr);
	vkDestroySampler(m_device, m_skyboxSampler, nullptr);
	destroySkybox(m_skybox);
	destroyModel(m_model);

	for(auto [view, _] : m_bloomMips) {
		vkDestroyImageView(m_device, view, nullptr);
	}

	destroyImage(m_shadowMap);
	destroyImage(m_brdfIntegralTex);
	destroyImage(m_colorTarget);
	destroyImage(m_bloomTarget);
	destroyImage(m_depthTarget);
	destroyImage(m_uiTarget);
	destroyBuffer(m_oitBuffer);
	destroyBuffer(m_poissonDiskBuffer);
	
	for(VkImageView view : m_swapchainImageViews) {
		vkDestroyImageView(m_device, view, nullptr);
	}

	for(VkSemaphore sem : m_swapchainSems) {
		vkDestroySemaphore(m_device, sem, nullptr);
	}

	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroyDevice(m_device, nullptr);

	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyInstance(m_instance, nullptr);

	NFD_Quit();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}