#include "pipeline.hpp"
#include <utility>
#include <tbrs/vk_util.hpp>

void Pipeline::bind(VkCommandBuffer cmd) {
	vkCmdBindPipeline(cmd, m_bindpoint, m_pipeline);
}

Pipeline::Pipeline(VkDevice device, VkPipelineLayout layout, ShaderCode cs) : m_device(device), m_bindpoint(VK_PIPELINE_BIND_POINT_COMPUTE) {
	vkCreateComputePipelines(m_device, nullptr, 1, ptr(VkComputePipelineCreateInfo{
		.stage = {
			.pNext = ptr(VkShaderModuleCreateInfo{
				.codeSize = cs.size() * sizeof(u32),
				.pCode = cs.begin()
			}),
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.pName = "main"
		},
		.layout = layout
	}), nullptr, &m_pipeline);
}

Pipeline::Pipeline(VkDevice device, VkPipelineLayout layout, ShaderCode vs, ShaderCode fs, VkFormat colorFmt, VkFormat depthFmt, VkCullModeFlagBits cullMode, VkCompareOp compareOp, bool depthWrite)
	: m_device(device), m_bindpoint(VK_PIPELINE_BIND_POINT_GRAPHICS) {

	vkCreateGraphicsPipelines(m_device, nullptr, 1, ptr(VkGraphicsPipelineCreateInfo{
		.pNext = ptr(VkPipelineRenderingCreateInfo{
			.colorAttachmentCount = static_cast<u32>(colorFmt != VK_FORMAT_UNDEFINED),
			.pColorAttachmentFormats = &colorFmt,
			.depthAttachmentFormat = depthFmt
		}),
		.stageCount = fs.size() == 0 ? 1u : 2u,
		.pStages = ptr({
			VkPipelineShaderStageCreateInfo{
				.pNext = ptr(VkShaderModuleCreateInfo{
					.codeSize = vs.size() * sizeof(u32),
					.pCode = vs.begin()
				}),
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.pName = "main"
			},
			VkPipelineShaderStageCreateInfo{
				.pNext = ptr(VkShaderModuleCreateInfo{
					.codeSize = fs.size() * sizeof(u32),
					.pCode = fs.begin()
				}),
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pName = "main"
			},
		}),
		.pVertexInputState = ptr(VkPipelineVertexInputStateCreateInfo{}),
		.pInputAssemblyState = ptr(VkPipelineInputAssemblyStateCreateInfo{.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST }),
		.pViewportState = ptr(VkPipelineViewportStateCreateInfo{.viewportCount = 1, .scissorCount = 1 }),
		.pRasterizationState = ptr(VkPipelineRasterizationStateCreateInfo{.cullMode = static_cast<VkCullModeFlags>(cullMode), .lineWidth = 1.0f }),
		.pMultisampleState = ptr(VkPipelineMultisampleStateCreateInfo{.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT }),
		.pDepthStencilState = ptr(VkPipelineDepthStencilStateCreateInfo{.depthTestEnable = true, .depthWriteEnable = depthWrite, .depthCompareOp = compareOp }),
		.pColorBlendState = ptr(VkPipelineColorBlendStateCreateInfo{.attachmentCount = 1, .pAttachments = ptr(VkPipelineColorBlendAttachmentState{.colorWriteMask = colorComponentAll() })}),
		.pDynamicState = ptr(VkPipelineDynamicStateCreateInfo{.dynamicStateCount = 2, .pDynamicStates = ptr({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }) }),
		.layout = layout
	}), nullptr, &m_pipeline);
}

Pipeline::~Pipeline() {
	if(m_device) {
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
	}
}

Pipeline::Pipeline(Pipeline&& src) {
	memcpy(this, &src, sizeof(Pipeline));
	memset(&src, 0, sizeof(Pipeline));
}
Pipeline& Pipeline::operator=(Pipeline&& src) {
	this->~Pipeline();
	new (this) Pipeline(std::move(src)); return *this;
};