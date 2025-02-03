#include <vulkan/vulkan.h>

VkImageSubresourceRange colorSubresourceRange() {
    return VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
}

VkImageSubresourceLayers colorSubresourceLayers() {
    return VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
}

VkImageSubresourceRange depthSubresourceRange() {
    return VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
}

VkImageSubresourceLayers depthSubresourceLayers() {
    return VkImageSubresourceLayers{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
}

VkColorComponentFlags colorComponentAll() {
    return VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}

auto* ptr(auto&& val) {
    return &val;
}

template<typename T>
const auto* ptr(std::initializer_list<T> val) {
    return val.begin();
}