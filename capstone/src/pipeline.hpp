#ifndef PIPELINE_H
#define PIPELINE_H

#include <volk/volk.h>
#include <initializer_list>
#include <tbrs/types.hpp>

class Pipeline {
	public:
		using ShaderCode = const std::initializer_list<u32>&;
	
		void bind(VkCommandBuffer cmd);
	
		Pipeline(VkDevice device, VkPipelineLayout layout, ShaderCode cs);
		Pipeline(VkDevice device, VkPipelineLayout layout, ShaderCode vs, ShaderCode fs, VkFormat colorFmt, VkFormat depthFmt, VkCullModeFlagBits cullMode, VkCompareOp compareOp, bool depthWrite);
		~Pipeline();
	
		Pipeline(const Pipeline&) = delete;
		Pipeline& operator=(const Pipeline&) = delete;
		Pipeline(Pipeline&& src);
		Pipeline& operator=(Pipeline&& src);
	
	private:
		VkDevice m_device;
		VkPipeline m_pipeline;
		VkPipelineBindPoint m_bindpoint;
};

#endif