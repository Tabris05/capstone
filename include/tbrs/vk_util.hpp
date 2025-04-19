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

inline glm::mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar) {
    return glm::mat4(
         2.0f / (right - left),            0.0f,                             0.0f,                  0.0f,
         0.0f,                             2.0f / (bottom - top),            0.0f,                  0.0f,
         0.0f,                             0.0f,                             1.0f / (zNear - zFar), 0.0f,
        -(right + left) / (right - left), -(bottom + top) / (bottom - top), -zFar / (zNear - zFar), 1.0f
    );
}

auto* ptr(auto&& val) {
    return &val;
}

template<typename T>
const auto* ptr(std::initializer_list<T> val) {
    return val.begin();
}

inline bool vkuFormatIsSRGB(VkFormat format) {
    switch(format) {
    case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
    case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
        return true;
    default:
        return false;
    }
}

#endif