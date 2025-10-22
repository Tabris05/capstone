#ifndef VK_UTIL_HPP
#define VK_UTIL_HPP

#include <initializer_list>
#include <volk/volk.h>
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

enum class PipelineStage {
    None = 0,

    PreRasterRead = 1 << 0,
    FragmentRead = 1 << 1,
    ComputeRead = 1 << 2,

    PreRasterWrite = 1 << 3,
    FragmentWrite = 1 << 4,
    ComputeWrite = 1 << 5,

    CopyRead = 1 << 6,
    CopyWrite = 1 << 7,

    DSReadOnly = 1 << 8,
    DSTarget = 1 << 9,
    ColorTarget = 1 << 10,

    PreRaster = PreRasterRead | PreRasterWrite,
    Fragment = FragmentRead | FragmentWrite,
    Compute = ComputeRead | ComputeWrite,

    Copy = CopyRead | CopyWrite,
    
    Read = PreRasterRead | FragmentRead | ComputeRead | CopyRead | DSReadOnly,
    Write = PreRasterWrite | FragmentWrite | ComputeWrite | CopyWrite | DSTarget | ColorTarget,
    All = Read | Write
};

inline PipelineStage operator&(PipelineStage lhs, PipelineStage rhs) {
    return static_cast<PipelineStage>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
}

inline PipelineStage operator|(PipelineStage lhs, PipelineStage rhs) {
    return static_cast<PipelineStage>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

static void pipeStageToBarrierBits(PipelineStage usages, VkPipelineStageFlagBits2& outStages, VkAccessFlagBits2& outAccess) {
    if((usages & PipelineStage::PreRasterRead) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT
                   | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
        outAccess |= VK_ACCESS_2_SHADER_READ_BIT;
    }

    if((usages & PipelineStage::PreRasterWrite) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT
            | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
        outAccess |= VK_ACCESS_2_SHADER_WRITE_BIT;
    }

    if((usages & PipelineStage::FragmentRead) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        outAccess |= VK_ACCESS_2_SHADER_READ_BIT;
    }

    if((usages & PipelineStage::FragmentWrite) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        outAccess |= VK_ACCESS_2_SHADER_WRITE_BIT;
    }

    if((usages & PipelineStage::ComputeRead) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        outAccess |= VK_ACCESS_2_SHADER_READ_BIT;
    }

    if((usages & PipelineStage::ComputeWrite) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        outAccess |= VK_ACCESS_2_SHADER_WRITE_BIT;
    }

    if((usages & PipelineStage::CopyRead) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        outAccess |= VK_ACCESS_2_TRANSFER_READ_BIT;
    }

    if((usages & PipelineStage::CopyWrite) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        outAccess |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }

    if((usages & PipelineStage::DSReadOnly) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        outAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }

    if((usages & PipelineStage::DSTarget) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        outAccess |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    if((usages & PipelineStage::ColorTarget) != PipelineStage::None) {
        outStages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        outAccess |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
}

struct Barrier {
    PipelineStage src;
    PipelineStage dst;
};

inline void vkCmdBarrier(VkCommandBuffer cmd, PipelineStage src, PipelineStage dst) {

    VkMemoryBarrier2 barrier{};
    pipeStageToBarrierBits(src, barrier.srcStageMask, barrier.srcAccessMask);
    pipeStageToBarrierBits(dst, barrier.dstStageMask, barrier.dstAccessMask);

    vkCmdPipelineBarrier2(cmd, ptr(VkDependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    }));
}

inline void vkCmdBarrier(VkCommandBuffer cmd, std::initializer_list<Barrier> barriers) {

    VkMemoryBarrier2 barrier{};
    for(auto [src, dst] : barriers) {
        pipeStageToBarrierBits(src, barrier.srcStageMask, barrier.srcAccessMask);
        pipeStageToBarrierBits(dst, barrier.dstStageMask, barrier.dstAccessMask);
    }

    vkCmdPipelineBarrier2(cmd, ptr(VkDependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    }));
}

inline void vkCmdInitializeColorImage(VkCommandBuffer cmd, VkImage image) {
    vkCmdPipelineBarrier2(cmd, ptr(VkDependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = image,
            .subresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }
        })
    }));
}

inline void vkCmdInitializeDepthImage(VkCommandBuffer cmd, VkImage image) {
    vkCmdPipelineBarrier2(cmd, ptr(VkDependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = image,
            .subresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }
        })
    }));
}

inline void vkCmdPreparePresent(VkCommandBuffer cmd, VkImage image) {
    vkCmdPipelineBarrier2(cmd, ptr(VkDependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = image,
            .subresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }
        })
    }));
}

#endif