#ifndef VK_UTIL_HPP
#define VK_UTIL_HPP

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "types.hpp"

inline VkImageSubresourceRange colorSubresourceRange() {
    return VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
}

inline VkImageSubresourceLayers colorSubresourceLayers() {
    return VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, VK_REMAINING_ARRAY_LAYERS };
}

inline VkImageSubresourceRange depthSubresourceRange() {
    return VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
}

inline VkImageSubresourceLayers depthSubresourceLayers() {
    return VkImageSubresourceLayers{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, VK_REMAINING_ARRAY_LAYERS };
}

inline VkColorComponentFlags colorComponentAll() {
    return VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}

inline glm::mat4 perspective(f32 fovy, f32 aspect, f32 zNear) {
    f32 f = 1.0f / std::tanf(fovy * 0.5f);
    return glm::mat4(
        f / aspect,  0.0f,  0.0f,    0.0f,
        0.0f,        -f,    0.0f,    0.0f,
        0.0f,        0.0f,  0.0f,   -1.0f,
        0.0f,        0.0f,  zNear,   0.0f
    );
}

auto* ptr(auto&& val) {
    return &val;
}

template<typename T>
const auto* ptr(std::initializer_list<T> val) {
    return val.begin();
}

#endif