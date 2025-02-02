#include <vulkan/vulkan.h>
VkImageSubresourceRange colorSubresource() {
    return VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
}

VkImageSubresourceRange depthSubresource() {
    return VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
}

auto* ptr(auto&& val) {
    return &val;
}

//const auto* ptr(std::initializer_list val) {
//    return val.begin();
//}