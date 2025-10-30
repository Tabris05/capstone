#include "context.hpp"
#include <vector>
#include <ranges>
#include <glfw/glfw3.h>
#include <tbrs/vk_util.hpp>

VkInstance VulkanContext::instance() {
	return m_instance;
}

VkDevice VulkanContext::device() {
	return m_device;
}

VkPhysicalDevice VulkanContext::physicalDevice() {
	return m_physicalDevice;
}

VkQueue VulkanContext::graphicsQueue() {
	return m_graphicsQueue;
}

VkQueue VulkanContext::computeQueue() {
	return m_computeQueue;
}

VkQueue VulkanContext::transferQueue() {
	return m_transferQueue;
}

u32* VulkanContext::queueFamilies() {
	return m_queueFamilies;
}

u32 VulkanContext::getMemoryIndex(VkMemoryPropertyFlags flags, u32 mask) {
	for(u32 idx = 0; idx < m_memProps.memoryTypeCount; idx++) {
		if(((1 << idx) & mask) && (m_memProps.memoryTypes[idx].propertyFlags & flags) == flags) {
			return idx;
		}
	}
}

VulkanContext::VulkanContext() {
	// volk and VkInstance
	{
		volkInitialize();

		u32 glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		vkCreateInstance(ptr(VkInstanceCreateInfo{
			.pApplicationInfo = ptr(VkApplicationInfo{.apiVersion = VK_API_VERSION_1_4 }),
			.enabledExtensionCount = glfwExtensionCount,
			.ppEnabledExtensionNames = glfwExtensions
		}), nullptr, &m_instance);

		volkLoadInstanceOnly(m_instance);
	}

	// VkPhysicalDevice and VkPhysicalDeviceMemoryProperties
	{
		vkEnumeratePhysicalDevices(m_instance, ptr(1u), &m_physicalDevice);
		vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProps);
	}

	// VkDevice and VkQueues
	{
		m_queueFamilies[0] = getQueue(VK_QUEUE_GRAPHICS_BIT);
		m_queueFamilies[1] = getQueue(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
		m_queueFamilies[2] = getQueue(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
		vkCreateDevice(m_physicalDevice, ptr(VkDeviceCreateInfo{
			.pNext = ptr(VkPhysicalDeviceFeatures2{
				.pNext = ptr(VkPhysicalDeviceVulkan11Features{
					.pNext = ptr(VkPhysicalDeviceVulkan12Features{
						.pNext = ptr(VkPhysicalDeviceVulkan13Features{
							.pNext = ptr(VkPhysicalDeviceVulkan14Features{
								.pNext = ptr(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT{
									.pNext = ptr(VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT{
										.pNext = ptr(VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR{.shaderMaximalReconvergence = true }),
										.fragmentShaderPixelInterlock = true
									}),
									.mutableDescriptorType = true
								}),
								.maintenance5 = true,
								.pushDescriptor = true
							}),
							.synchronization2 = true,
							.dynamicRendering = true
						}),
						.shaderSampledImageArrayNonUniformIndexing = true,
						.shaderStorageImageArrayNonUniformIndexing = true,
						.descriptorBindingSampledImageUpdateAfterBind = true,
						.descriptorBindingStorageBufferUpdateAfterBind = true,
						.descriptorBindingPartiallyBound = true,
						.descriptorBindingVariableDescriptorCount = true, // foo: delete
						.runtimeDescriptorArray = true,
						.scalarBlockLayout = true,
						.timelineSemaphore = true,
						.bufferDeviceAddress = true,
						.vulkanMemoryModel = true,
						.vulkanMemoryModelDeviceScope = true,
						.vulkanMemoryModelAvailabilityVisibilityChains = true
					}),
					.shaderDrawParameters = true
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
					.queueFamilyIndex = m_queueFamilies[0],
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				},
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_queueFamilies[1],
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				},
				VkDeviceQueueCreateInfo{
					.queueFamilyIndex = m_queueFamilies[2],
					.queueCount = 1,
					.pQueuePriorities = ptr(1.0f)
				}
			}),
			.enabledExtensionCount = 4,
			.ppEnabledExtensionNames = ptr<const char*>({
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
				VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,
				VK_KHR_SHADER_MAXIMAL_RECONVERGENCE_EXTENSION_NAME
			}),
		}), nullptr, &m_device);

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_queueFamilies[0], 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_queueFamilies[1], 0, &m_computeQueue);
		vkGetDeviceQueue(m_device, m_queueFamilies[2], 0, &m_transferQueue);
	}
}

VulkanContext::~VulkanContext() {
	vkDestroyDevice(m_device, nullptr);
	vkDestroyInstance(m_instance, nullptr);
}

VulkanContext::VulkanContext(VulkanContext&& src) {
	memcpy(this, &src, sizeof(VulkanContext));
	memset(&src, 0, sizeof(VulkanContext));
}

VulkanContext& VulkanContext::operator=(VulkanContext&& src) {
	this->~VulkanContext();
	new (this) VulkanContext(std::move(src)); return *this;
};

u32 VulkanContext::getQueue(VkQueueFlags include, VkQueueFlags exclude) {
	u32 size = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &size, nullptr);

	std::vector<VkQueueFamilyProperties> queueProperties(size);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &size, queueProperties.data());

	for(auto [idx, queueFamily] : std::views::enumerate(queueProperties)) {
		if((queueFamily.queueFlags & include) && !(queueFamily.queueFlags & exclude)) {
			return idx;
		}
	}
}